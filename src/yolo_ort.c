#include "yolo_ort.h"
#include "yolo_post.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef const OrtApiBase *(ORT_API_CALL *yolo_ort_get_base_fn)(void);

struct yolo_ort_session {
    HMODULE dll;
    const OrtApi *api;
    OrtEnv *env;
    OrtSession *session;
    OrtMemoryInfo *memory_info;
    char *input_name;
    char *output_name;
    OrtValue *output_value;
    OrtRunOptions *active_run_options;
    CRITICAL_SECTION run_options_lock;
    int lock_initialized;
};

static void yolo_ort_copy_error(char *error, size_t capacity,
                                const char *message)
{
    if (error != NULL && capacity != 0) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static int yolo_ort_status_error(yolo_ort_session_t *session, OrtStatus *status,
                                char *error, size_t capacity)
{
    const char *message;

    if (status == NULL) {
        yolo_ort_copy_error(error, capacity, "");
        return 0;
    }
    message = session->api->GetErrorMessage(status);
    yolo_ort_copy_error(error, capacity,
                        message != NULL ? message : "ONNX Runtime error");
    session->api->ReleaseStatus(status);
    return -1;
}

static int yolo_ort_validate_tensor(yolo_ort_session_t *session,
                                   int is_input, char *error, size_t capacity)
{
    OrtStatus *status;
    OrtTypeInfo *type_info = NULL;
    const OrtTensorTypeAndShapeInfo *tensor_info = NULL;
    ONNXTensorElementDataType element_type;
    size_t dimension_count = 0;
    int64_t dimensions[4];
    const int64_t expected_input[4] = { 1, 3, 640, 640 };
    const int64_t expected_output[3] = { 1, 6, 8400 };
    const int64_t *expected = is_input ? expected_input : expected_output;
    const size_t expected_count = is_input ? 4U : 3U;
    size_t i;

    status = is_input ?
        session->api->SessionGetInputTypeInfo(session->session, 0, &type_info) :
        session->api->SessionGetOutputTypeInfo(session->session, 0,
                                               &type_info);
    if (yolo_ort_status_error(session, status, error, capacity) != 0) {
        return -1;
    }
    status = session->api->CastTypeInfoToTensorInfo(type_info, &tensor_info);
    if (yolo_ort_status_error(session, status, error, capacity) != 0) {
        session->api->ReleaseTypeInfo(type_info);
        return -1;
    }
    if (tensor_info == NULL) {
        yolo_ort_copy_error(error, capacity, "Model tensor type is unsupported");
        session->api->ReleaseTypeInfo(type_info);
        return -1;
    }
    status = session->api->GetTensorElementType(tensor_info, &element_type);
    if (yolo_ort_status_error(session, status, error, capacity) != 0) {
        session->api->ReleaseTypeInfo(type_info);
        return -1;
    }
    status = session->api->GetDimensionsCount(tensor_info, &dimension_count);
    if (yolo_ort_status_error(session, status, error, capacity) != 0) {
        session->api->ReleaseTypeInfo(type_info);
        return -1;
    }
    if (element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        dimension_count != expected_count) {
        yolo_ort_copy_error(error, capacity, "Model tensor shape is unsupported");
        session->api->ReleaseTypeInfo(type_info);
        return -1;
    }
    status = session->api->GetDimensions(tensor_info, dimensions,
                                         expected_count);
    if (yolo_ort_status_error(session, status, error, capacity) != 0) {
        session->api->ReleaseTypeInfo(type_info);
        return -1;
    }
    for (i = 0; i < expected_count; ++i) {
        if (dimensions[i] != expected[i]) {
            yolo_ort_copy_error(error, capacity,
                                "Model tensor shape is unsupported");
            session->api->ReleaseTypeInfo(type_info);
            return -1;
        }
    }
    session->api->ReleaseTypeInfo(type_info);
    return 0;
}

static void yolo_ort_release_partial(yolo_ort_session_t *session)
{
    const OrtApi *api;

    if (session == NULL) {
        return;
    }
    api = session->api;
    if (api != NULL) {
        if (session->output_value != NULL) {
            api->ReleaseValue(session->output_value);
        }
        if (session->memory_info != NULL) {
            api->ReleaseMemoryInfo(session->memory_info);
        }
        if (session->session != NULL) {
            api->ReleaseSession(session->session);
        }
        if (session->env != NULL) {
            api->ReleaseEnv(session->env);
        }
        if (session->input_name != NULL || session->output_name != NULL) {
            OrtAllocator *allocator = NULL;

            if (api->GetAllocatorWithDefaultOptions(&allocator) == NULL &&
                allocator != NULL) {
                if (session->input_name != NULL) {
                    (void)allocator->Free(allocator, session->input_name);
                }
                if (session->output_name != NULL) {
                    (void)allocator->Free(allocator, session->output_name);
                }
            }
        }
    }
    if (session->lock_initialized) {
        DeleteCriticalSection(&session->run_options_lock);
    }
    if (session->dll != NULL) {
        FreeLibrary(session->dll);
    }
    free(session);
}

int yolo_ort_load(yolo_ort_session_t **session_out, const char *model_path,
                  char *error, size_t error_capacity)
{
    char dll_path[MAX_PATH];
    char *slash;
    DWORD path_length;
    FARPROC proc;
    yolo_ort_get_base_fn get_api_base;
    const OrtApiBase *api_base;
    yolo_ort_session_t *session;
    wchar_t wide_model_path[MAX_PATH];
    OrtSessionOptions *session_options = NULL;
    OrtAllocator *allocator = NULL;
    OrtStatus *status;
    size_t input_count = 0;
    size_t output_count = 0;

    if (session_out == NULL || model_path == NULL) {
        yolo_ort_copy_error(error, error_capacity, "Invalid model arguments");
        return -1;
    }
    *session_out = NULL;
    path_length = GetModuleFileNameA(NULL, dll_path, MAX_PATH);
    if (path_length == 0 || path_length >= MAX_PATH) {
        yolo_ort_copy_error(error, error_capacity,
                            "Cannot locate application directory");
        return -1;
    }
    slash = strrchr(dll_path, '\\');
    if (slash == NULL) {
        slash = strrchr(dll_path, '/');
    }
    if (slash == NULL ||
        (size_t)(slash - dll_path) + 1U + sizeof("onnxruntime.dll") >
            sizeof(dll_path)) {
        yolo_ort_copy_error(error, error_capacity, "Invalid DLL path");
        return -1;
    }
    (void)strcpy(slash + 1, "onnxruntime.dll");

    session = (yolo_ort_session_t *)calloc(1, sizeof(*session));
    if (session == NULL) {
        yolo_ort_copy_error(error, error_capacity, "Out of memory");
        return -1;
    }
    session->dll = LoadLibraryExA(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (session->dll == NULL) {
        yolo_ort_copy_error(error, error_capacity,
                            "Cannot load onnxruntime.dll");
        free(session);
        return -1;
    }
    proc = GetProcAddress(session->dll, "OrtGetApiBase");
    get_api_base = (yolo_ort_get_base_fn)(void (*)(void))proc;
    if (get_api_base == NULL) {
        yolo_ort_copy_error(error, error_capacity,
                            "ONNX Runtime API entry point is missing");
        yolo_ort_release_partial(session);
        return -1;
    }
    api_base = get_api_base();
    if (api_base == NULL || api_base->GetApi == NULL) {
        yolo_ort_copy_error(error, error_capacity,
                            "ONNX Runtime API is unavailable");
        yolo_ort_release_partial(session);
        return -1;
    }
    session->api = api_base->GetApi(ORT_API_VERSION);
    if (session->api == NULL) {
        yolo_ort_copy_error(error, error_capacity,
                            "ONNX Runtime DLL version is too old");
        yolo_ort_release_partial(session);
        return -1;
    }

    InitializeCriticalSection(&session->run_options_lock);
    session->lock_initialized = 1;
    status = session->api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "roi-analyzer",
                                     &session->env);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }
    status = session->api->CreateSessionOptions(&session_options);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }
    status = session->api->SetSessionGraphOptimizationLevel(
        session_options, ORT_ENABLE_ALL);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        session->api->ReleaseSessionOptions(session_options);
        yolo_ort_release_partial(session);
        return -1;
    }
    if (MultiByteToWideChar(CP_ACP, 0, model_path, -1, wide_model_path,
                            MAX_PATH) == 0) {
        yolo_ort_copy_error(error, error_capacity, "Invalid model path");
        session->api->ReleaseSessionOptions(session_options);
        yolo_ort_release_partial(session);
        return -1;
    }
    status = session->api->CreateSession(session->env, wide_model_path,
                                         session_options, &session->session);
    session->api->ReleaseSessionOptions(session_options);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }

    status = session->api->SessionGetInputCount(session->session, &input_count);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }
    status = session->api->SessionGetOutputCount(session->session,
                                                 &output_count);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }
    if (input_count != 1 || output_count != 1) {
        yolo_ort_copy_error(error, error_capacity,
                            "Model input/output count is unsupported");
        yolo_ort_release_partial(session);
        return -1;
    }
    if (yolo_ort_validate_tensor(session, 1, error, error_capacity) != 0 ||
        yolo_ort_validate_tensor(session, 0, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }
    status = session->api->GetAllocatorWithDefaultOptions(&allocator);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }
    status = session->api->SessionGetInputName(session->session, 0, allocator,
                                               &session->input_name);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }
    status = session->api->SessionGetOutputName(session->session, 0, allocator,
                                                &session->output_name);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }
    status = session->api->CreateCpuMemoryInfo(OrtArenaAllocator,
                                               OrtMemTypeDefault,
                                               &session->memory_info);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        yolo_ort_release_partial(session);
        return -1;
    }

    *session_out = session;
    yolo_ort_copy_error(error, error_capacity, "");
    return 0;
}

void yolo_ort_unload(yolo_ort_session_t *session)
{
    yolo_ort_release_partial(session);
}

const OrtApi *yolo_ort_get_api(const yolo_ort_session_t *session)
{
    return session != NULL ? session->api : NULL;
}

int yolo_ort_register_run_options(yolo_ort_session_t *session,
                                  OrtRunOptions *run_options)
{
    if (session == NULL || run_options == NULL) {
        return -1;
    }
    EnterCriticalSection(&session->run_options_lock);
    if (session->active_run_options != NULL) {
        LeaveCriticalSection(&session->run_options_lock);
        return -1;
    }
    session->active_run_options = run_options;
    LeaveCriticalSection(&session->run_options_lock);
    return 0;
}

void yolo_ort_unregister_run_options(yolo_ort_session_t *session,
                                     OrtRunOptions *run_options)
{
    if (session == NULL) {
        return;
    }
    EnterCriticalSection(&session->run_options_lock);
    if (session->active_run_options == run_options) {
        session->active_run_options = NULL;
    }
    LeaveCriticalSection(&session->run_options_lock);
}

int yolo_ort_terminate(yolo_ort_session_t *session, char *error,
                       size_t error_capacity)
{
    OrtStatus *status = NULL;

    if (session == NULL) {
        yolo_ort_copy_error(error, error_capacity, "Invalid session");
        return -1;
    }
    EnterCriticalSection(&session->run_options_lock);
    if (session->active_run_options != NULL) {
        status = session->api->RunOptionsSetTerminate(
            session->active_run_options);
    }
    LeaveCriticalSection(&session->run_options_lock);
    return yolo_ort_status_error(session, status, error, error_capacity);
}

int yolo_ort_run(yolo_ort_session_t *session, const float *input,
                 OrtRunOptions *run_options, const float **output,
                 char *error, size_t error_capacity)
{
    const int64_t input_dimensions[4] = { 1, 3, 640, 640 };
    const size_t input_bytes =
        (size_t)3 * YOLO_INPUT_SIZE * YOLO_INPUT_SIZE * sizeof(float);
    OrtValue *input_value = NULL;
    OrtValue *output_value = NULL;
    OrtStatus *status;
    float *output_data = NULL;
    int run_options_registered;

    if (session == NULL || input == NULL || run_options == NULL ||
        output == NULL) {
        yolo_ort_copy_error(error, error_capacity, "Invalid inference arguments");
        return -1;
    }
    *output = NULL;
    EnterCriticalSection(&session->run_options_lock);
    run_options_registered =
        session->active_run_options == run_options;
    LeaveCriticalSection(&session->run_options_lock);
    if (!run_options_registered) {
        yolo_ort_copy_error(error, error_capacity,
                            "Run options are not registered");
        return -1;
    }
    status = session->api->CreateTensorWithDataAsOrtValue(
        session->memory_info, (void *)input, input_bytes, input_dimensions, 4,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input_value);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        return -1;
    }
    if (session->output_value != NULL) {
        session->api->ReleaseValue(session->output_value);
        session->output_value = NULL;
    }
    status = session->api->Run(
        session->session, run_options,
        (const char *const *)&session->input_name,
        (const OrtValue *const *)&input_value, 1,
        (const char *const *)&session->output_name, 1, &output_value);
    session->api->ReleaseValue(input_value);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        return -1;
    }
    session->output_value = output_value;
    status = session->api->GetTensorMutableData(session->output_value,
                                                (void **)&output_data);
    if (yolo_ort_status_error(session, status, error, error_capacity) != 0) {
        session->api->ReleaseValue(session->output_value);
        session->output_value = NULL;
        return -1;
    }
    *output = output_data;
    yolo_ort_copy_error(error, error_capacity, "");
    return 0;
}
