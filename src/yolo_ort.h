#ifndef ROI_YOLO_ORT_H
#define ROI_YOLO_ORT_H

#include <stddef.h>

#include <onnxruntime_c_api.h>

typedef struct yolo_ort_session yolo_ort_session_t;

int yolo_ort_load(yolo_ort_session_t **session, const char *model_path,
                  char *error, size_t error_capacity);
void yolo_ort_unload(yolo_ort_session_t *session);
const OrtApi *yolo_ort_get_api(const yolo_ort_session_t *session);
int yolo_ort_register_run_options(yolo_ort_session_t *session,
                                  OrtRunOptions *run_options);
void yolo_ort_unregister_run_options(yolo_ort_session_t *session,
                                     OrtRunOptions *run_options);
int yolo_ort_terminate(yolo_ort_session_t *session, char *error,
                       size_t error_capacity);
int yolo_ort_run(yolo_ort_session_t *session, const float *input,
                 OrtRunOptions *run_options, const float **output,
                 char *error, size_t error_capacity);

#endif
