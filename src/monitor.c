#include "monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
#include <shlobj.h>

#include "compare.h"
#include "settings.h"

#define IDD_MONITOR_SETTINGS 210
#define IDC_MON_PATH1 211
#define IDC_MON_PATH2 212
#define IDC_MON_PATH3 213
#define IDC_MON_BROWSE1 214
#define IDC_MON_BROWSE2 215
#define IDC_MON_BROWSE3 216
#define IDC_MON_ACTIVE1 217
#define IDC_MON_ACTIVE2 218
#define IDC_MON_ACTIVE3 219

#define SELF_RENAME_CAPACITY 16

typedef struct {
    HANDLE directory;
    OVERLAPPED overlapped;
    union {
        DWORD alignment;
        BYTE bytes[4096];
    } buffer;
    int config_index;
} monitor_watch_t;

typedef struct {
    char path[MAX_PATH];
    ULONGLONG tick;
} self_rename_t;

static monitor_config_t s_configs[MONITOR_MAX_PATHS];
static volatile LONG s_status[MONITOR_MAX_PATHS] = {
    MONITOR_STATUS_UNSET, MONITOR_STATUS_UNSET, MONITOR_STATUS_UNSET
};
static HWND s_hwnd_notify;
static HANDLE s_thread;
static HANDLE s_stop_event;
static CRITICAL_SECTION s_watch_lock;
static BOOL s_watch_lock_initialized;
static HANDLE s_worker_directories[MONITOR_MAX_PATHS];
static self_rename_t s_self_renames[SELF_RENAME_CAPACITY];
static size_t s_self_rename_head;
static SRWLOCK s_self_rename_lock = SRWLOCK_INIT;

static void set_status(int index, monitor_status_t status)
{
    InterlockedExchange(&s_status[index], (LONG)status);
}

void Monitor_NoteSelfRename(const char *abs_path)
{
    self_rename_t *entry;
    char full_path[MAX_PATH];
    DWORD full_path_length;

    if (!abs_path || !abs_path[0])
        return;
    full_path_length = GetFullPathNameA(abs_path, MAX_PATH, full_path, NULL);
    if (full_path_length == 0 || full_path_length >= (DWORD)MAX_PATH)
        return;
    AcquireSRWLockExclusive(&s_self_rename_lock);
    entry = &s_self_renames[s_self_rename_head];
    lstrcpynA(entry->path, full_path, MAX_PATH);
    entry->tick = GetTickCount64();
    s_self_rename_head = (s_self_rename_head + 1) % SELF_RENAME_CAPACITY;
    ReleaseSRWLockExclusive(&s_self_rename_lock);
}

static BOOL is_recent_self_rename(const char *path)
{
    char full_path[MAX_PATH];
    DWORD full_path_length;
    ULONGLONG now;
    size_t i;
    BOOL found = FALSE;

    if (!path || !path[0])
        return FALSE;
    full_path_length = GetFullPathNameA(path, MAX_PATH, full_path, NULL);
    if (full_path_length == 0 || full_path_length >= (DWORD)MAX_PATH)
        return FALSE;
    now = GetTickCount64();
    AcquireSRWLockShared(&s_self_rename_lock);
    for (i = 0; i < SELF_RENAME_CAPACITY; i++) {
        if (_stricmp(s_self_renames[i].path, full_path) == 0 &&
            now - s_self_renames[i].tick < 5000) {
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockShared(&s_self_rename_lock);
    return found;
}

BOOL Monitor_IsSelfRename(const char *path)
{
    return is_recent_self_rename(path);
}

static void debug_error(const char *operation, DWORD error)
{
    char message[160];
    _snprintf(message, sizeof(message),
              "ROI Analyzer monitor: %s failed (error %lu).\n",
              operation, (unsigned long)error);
    message[sizeof(message) - 1] = '\0';
    OutputDebugStringA(message);
}

static BOOL ensure_watch_lock(void)
{
    if (s_watch_lock_initialized)
        return TRUE;
    if (!InitializeCriticalSectionAndSpinCount(&s_watch_lock, 0x800)) {
        debug_error("InitializeCriticalSection", GetLastError());
        return FALSE;
    }
    s_watch_lock_initialized = TRUE;
    return TRUE;
}

static BOOL supported_image(const char *path)
{
    const char *extension = PathFindExtensionA(path);
    return extension &&
           (_stricmp(extension, ".png") == 0 ||
            _stricmp(extension, ".jpg") == 0 ||
            _stricmp(extension, ".jpeg") == 0 ||
            _stricmp(extension, ".bmp") == 0);
}

static BOOL issue_read(monitor_watch_t *watch)
{
    DWORD error;
    HANDLE event = watch->overlapped.hEvent;
    ResetEvent(event);
    ZeroMemory(&watch->overlapped, sizeof(watch->overlapped));
    watch->overlapped.hEvent = event;
    if (ReadDirectoryChangesW(watch->directory, watch->buffer.bytes,
                              sizeof(watch->buffer.bytes), FALSE,
                              FILE_NOTIFY_CHANGE_FILE_NAME |
                              FILE_NOTIFY_CHANGE_LAST_WRITE,
                              NULL, &watch->overlapped, NULL))
        return TRUE;
    error = GetLastError();
    if (error == ERROR_IO_PENDING)
        return TRUE;
    debug_error("ReadDirectoryChangesW", error);
    return FALSE;
}

static void post_file_path(const char *full_path)
{
    char *message_path;
    Sleep(500);
    if (!File_WaitForWriteComplete(full_path, 3000)) {
        OutputDebugStringA("ROI Analyzer monitor: file did not become stable; event dropped.\n");
        return;
    }
    if (is_recent_self_rename(full_path))
        return;
    message_path = _strdup(full_path);
    if (!message_path) {
        OutputDebugStringA("ROI Analyzer monitor: could not allocate a file path.\n");
        return;
    }
    if (!PostMessageA(s_hwnd_notify, WM_APP_NEW_FILE, 0,
                      (LPARAM)message_path)) {
        OutputDebugStringA("ROI Analyzer monitor: could not post a new-file notification.\n");
        free(message_path);
    }
}

static int collect_notifications(monitor_watch_t *watch,
                                 const monitor_config_t *config,
                                 DWORD bytes,
                                 char paths[256][MAX_PATH])
{
    DWORD offset = 0;
    int count = 0;
    const DWORD name_offset =
        (DWORD)FIELD_OFFSET(FILE_NOTIFY_INFORMATION, FileName);

    while (offset < bytes) {
        FILE_NOTIFY_INFORMATION *entry =
            (FILE_NOTIFY_INFORMATION *)(watch->buffer.bytes + offset);
        size_t name_length;
        wchar_t name[MAX_PATH];
        char acp_name[MAX_PATH], full_path[MAX_PATH];
        if (bytes - offset < name_offset + (DWORD)sizeof(WCHAR) ||
            entry->FileNameLength % (DWORD)sizeof(WCHAR) != 0 ||
            (size_t)entry->FileNameLength >
                (size_t)(bytes - offset - name_offset))
            break;
        name_length = entry->FileNameLength / sizeof(WCHAR);
        if (name_length > 0 && name_length < (size_t)MAX_PATH &&
            count < 256 &&
            (entry->Action == FILE_ACTION_ADDED ||
             entry->Action == FILE_ACTION_RENAMED_NEW_NAME)) {
            memcpy(name, entry->FileName, name_length * sizeof(WCHAR));
            name[name_length] = L'\0';
            if (wide_to_acp_strict(name, acp_name, sizeof(acp_name)) &&
                supported_image(acp_name) &&
                PathCombineA(full_path, config->path, acp_name) &&
                !is_recent_self_rename(full_path)) {
                lstrcpynA(paths[count], full_path, MAX_PATH);
                count++;
            }
        }
        if (entry->NextEntryOffset == 0)
            break;
        if (entry->NextEntryOffset < name_offset ||
            entry->NextEntryOffset > bytes - offset)
            break;
        offset += entry->NextEntryOffset;
    }
    return count;
}

static DWORD WINAPI monitor_worker(void *unused)
{
    monitor_watch_t watches[MONITOR_MAX_PATHS];
    monitor_config_t configs[MONITOR_MAX_PATHS];
    HANDLE events[MONITOR_MAX_PATHS + 1];
    int watch_count = 0, i;

    (void)unused;
    ZeroMemory(watches, sizeof(watches));
    CopyMemory(configs, s_configs, sizeof(configs));
    events[0] = s_stop_event;

    for (i = 0; i < MONITOR_MAX_PATHS; i++) {
        HANDLE directory;
        DWORD attributes;
        monitor_watch_t *watch;

        if (!configs[i].is_active || !configs[i].path[0])
            continue;
        attributes = GetFileAttributesA(configs[i].path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            debug_error("GetFileAttributesA monitor directory", GetLastError());
            set_status(i, MONITOR_STATUS_STOPPED);
            continue;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            debug_error("monitor path is not a directory", ERROR_DIRECTORY);
            set_status(i, MONITOR_STATUS_STOPPED);
            continue;
        }
        directory = CreateFileA(configs[i].path, FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS |
                                FILE_FLAG_OVERLAPPED, NULL);
        if (directory == INVALID_HANDLE_VALUE) {
            debug_error("CreateFileA directory", GetLastError());
            set_status(i, MONITOR_STATUS_STOPPED);
            continue;
        }
        watch = &watches[watch_count];
        watch->directory = directory;
        watch->config_index = i;
        watch->overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!watch->overlapped.hEvent) {
            debug_error("CreateEventA", GetLastError());
            CloseHandle(directory);
            watch->directory = INVALID_HANDLE_VALUE;
            set_status(i, MONITOR_STATUS_STOPPED);
            continue;
        }
        events[watch_count + 1] = watch->overlapped.hEvent;
        EnterCriticalSection(&s_watch_lock);
        s_worker_directories[watch_count] = directory;
        LeaveCriticalSection(&s_watch_lock);
        if (!issue_read(watch)) {
            EnterCriticalSection(&s_watch_lock);
            s_worker_directories[watch_count] = NULL;
            LeaveCriticalSection(&s_watch_lock);
            CloseHandle(watch->overlapped.hEvent);
            CloseHandle(directory);
            watch->directory = INVALID_HANDLE_VALUE;
            set_status(i, MONITOR_STATUS_STOPPED);
            continue;
        }
        set_status(i, MONITOR_STATUS_ACTIVE);
        watch_count++;
    }

    for (;;) {
        DWORD wait_result = WaitForMultipleObjects((DWORD)watch_count + 1,
                                                   events, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0)
            break;
        if (wait_result < WAIT_OBJECT_0 + 1 ||
            wait_result >= WAIT_OBJECT_0 + (DWORD)watch_count + 1) {
            debug_error("WaitForMultipleObjects", GetLastError());
            break;
        }
        {
            int slot = (int)(wait_result - WAIT_OBJECT_0 - 1);
            monitor_watch_t *watch = &watches[slot];
            DWORD bytes = 0;
            DWORD error = ERROR_SUCCESS;
            char paths[256][MAX_PATH];
            int path_count = 0, path_index;
            if (!GetOverlappedResult(watch->directory, &watch->overlapped,
                                     &bytes, FALSE))
                error = GetLastError();
            if (error == ERROR_NOTIFY_ENUM_DIR || bytes == 0) {
                OutputDebugStringA("ROI Analyzer monitor: notification buffer overflow; events dropped.\n");
            } else if (error != ERROR_SUCCESS) {
                debug_error("GetOverlappedResult", error);
            } else {
                path_count = collect_notifications(
                    watch, &configs[watch->config_index], bytes, paths);
            }
            if (WaitForSingleObject(s_stop_event, 0) == WAIT_OBJECT_0)
                break;
            if (!issue_read(watch)) {
                set_status(watch->config_index, MONITOR_STATUS_STOPPED);
                EnterCriticalSection(&s_watch_lock);
                s_worker_directories[slot] = NULL;
                LeaveCriticalSection(&s_watch_lock);
                CancelIoEx(watch->directory, NULL);
                CloseHandle(watch->directory);
                watch->directory = INVALID_HANDLE_VALUE;
                ResetEvent(watch->overlapped.hEvent);
            }
            for (path_index = 0; path_index < path_count; path_index++) {
                if (WaitForSingleObject(s_stop_event, 0) == WAIT_OBJECT_0)
                    break;
                post_file_path(paths[path_index]);
            }
        }
    }

    for (i = 0; i < watch_count; i++) {
        monitor_watch_t *watch = &watches[i];
        if (watch->directory != INVALID_HANDLE_VALUE) {
            CancelIoEx(watch->directory, &watch->overlapped);
            WaitForSingleObject(watch->overlapped.hEvent, INFINITE);
            EnterCriticalSection(&s_watch_lock);
            s_worker_directories[i] = NULL;
            LeaveCriticalSection(&s_watch_lock);
            CloseHandle(watch->directory);
        }
        if (watch->overlapped.hEvent)
            CloseHandle(watch->overlapped.hEvent);
        if (watch->config_index >= 0)
            set_status(watch->config_index, MONITOR_STATUS_STOPPED);
    }
    return 0;
}

static BOOL start_worker(void)
{
    int active = 0, i;

    if (s_thread)
        return TRUE;
    if (!s_hwnd_notify)
        return FALSE;
    if (!ensure_watch_lock())
        return FALSE;
    for (i = 0; i < MONITOR_MAX_PATHS; i++)
        if (s_configs[i].is_active && s_configs[i].path[0])
            active++;
    if (active == 0)
        return TRUE;
    s_stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!s_stop_event) {
        debug_error("CreateEventA stop", GetLastError());
        return FALSE;
    }
    s_thread = CreateThread(NULL, 0, monitor_worker, NULL, 0, NULL);
    if (!s_thread) {
        debug_error("CreateThread", GetLastError());
        CloseHandle(s_stop_event);
        s_stop_event = NULL;
        return FALSE;
    }
    return TRUE;
}

BOOL Monitor_Init(HWND hwnd_notify)
{
    int i, active = 0;
    char message[96];
    BOOL started;
    if (!hwnd_notify)
        return FALSE;
    s_hwnd_notify = hwnd_notify;
    if (!Settings_LoadMonitorConfigs(s_configs))
        return FALSE;
    for (i = 0; i < MONITOR_MAX_PATHS; i++)
        set_status(i, s_configs[i].path[0] ?
                   MONITOR_STATUS_STOPPED : MONITOR_STATUS_UNSET);
    started = start_worker();
    for (i = 0; i < MONITOR_MAX_PATHS; i++)
        if (s_configs[i].is_active && s_configs[i].path[0])
            active++;
    _snprintf(message, sizeof(message),
              "ROI Analyzer monitor: %d active path(s) configured.\n", active);
    message[sizeof(message) - 1] = '\0';
    OutputDebugStringA(message);
    return started;
}

void Monitor_Shutdown(void)
{
    int i;
    if (!s_thread)
        return;
    EnterCriticalSection(&s_watch_lock);
    for (i = 0; i < MONITOR_MAX_PATHS; i++)
        if (s_worker_directories[i])
            CancelIoEx(s_worker_directories[i], NULL);
    SetEvent(s_stop_event);
    LeaveCriticalSection(&s_watch_lock);
    WaitForSingleObject(s_thread, INFINITE);
    CloseHandle(s_thread);
    CloseHandle(s_stop_event);
    s_thread = NULL;
    s_stop_event = NULL;
}

BOOL Monitor_UpdateConfigs(const monitor_config_t configs[MONITOR_MAX_PATHS])
{
    monitor_config_t normalized[MONITOR_MAX_PATHS];
    int i;
    if (!configs)
        return FALSE;
    CopyMemory(normalized, configs, sizeof(normalized));
    for (i = 0; i < MONITOR_MAX_PATHS; i++) {
        normalized[i].path[MAX_PATH - 1] = '\0';
        normalized[i].is_active = normalized[i].is_active ? TRUE : FALSE;
    }
    if (!Settings_SaveMonitorConfigs(normalized))
        return FALSE;
    Monitor_Shutdown();
    CopyMemory(s_configs, normalized, sizeof(s_configs));
    for (i = 0; i < MONITOR_MAX_PATHS; i++)
        set_status(i, s_configs[i].path[0] ?
                   MONITOR_STATUS_STOPPED : MONITOR_STATUS_UNSET);
    return start_worker();
}

void Monitor_GetConfigs(monitor_config_t configs[MONITOR_MAX_PATHS])
{
    if (configs)
        CopyMemory(configs, s_configs, sizeof(s_configs));
}

monitor_status_t Monitor_GetStatus(int index, char *out_path, size_t cap)
{
    if (out_path && cap)
        out_path[0] = '\0';
    if (index < 0 || index >= MONITOR_MAX_PATHS)
        return MONITOR_STATUS_UNSET;
    if (out_path && cap) {
        strncpy(out_path, s_configs[index].path, cap - 1);
        out_path[cap - 1] = '\0';
    }
    {
        monitor_status_t status;
        status = (monitor_status_t)InterlockedCompareExchange(&s_status[index],
                                                              0, 0);
        return status;
    }
}

void Monitor_StartAll(void)
{
    if (!start_worker())
        debug_error("start monitor worker", GetLastError());
}

void Monitor_StopAll(void)
{
    Monitor_Shutdown();
}

BOOL File_WaitForWriteComplete(const char *path, DWORD timeout_ms)
{
    DWORD started;
    LONGLONG previous_size = 0;
    BOOL have_previous = FALSE;

    if (!path || !path[0])
        return FALSE;
    started = GetTickCount();
    for (;;) {
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        BOOL measured = FALSE;
        if (file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size;
            if (GetFileSizeEx(file, &size)) {
                if (have_previous && size.QuadPart == previous_size) {
                    CloseHandle(file);
                    return TRUE;
                }
                previous_size = size.QuadPart;
                have_previous = TRUE;
                measured = TRUE;
            }
            CloseHandle(file);
        }
        if (!measured)
            have_previous = FALSE;
        if (s_stop_event && WaitForSingleObject(s_stop_event, 0) == WAIT_OBJECT_0)
            return FALSE;
        if (GetTickCount() - started >= timeout_ms)
            return FALSE;
        Sleep(100);
    }
}

static void browse_for_folder(HWND dialog, int index)
{
    static const int path_ids[MONITOR_MAX_PATHS] = {
        IDC_MON_PATH1, IDC_MON_PATH2, IDC_MON_PATH3
    };
    BROWSEINFOA browse;
    char selected[MAX_PATH];
    LPITEMIDLIST item;
    selected[0] = '\0';
    ZeroMemory(&browse, sizeof(browse));
    browse.hwndOwner = dialog;
    browse.lpszTitle = "Select a folder to monitor";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    item = SHBrowseForFolderA(&browse);
    if (!item)
        return;
    if (SHGetPathFromIDListA(item, selected))
        SetDlgItemTextA(dialog, path_ids[index], selected);
    CoTaskMemFree(item);
}

static INT_PTR CALLBACK monitor_settings_proc(HWND dialog, UINT message,
                                               WPARAM wparam, LPARAM lparam)
{
    static const int path_ids[MONITOR_MAX_PATHS] = {
        IDC_MON_PATH1, IDC_MON_PATH2, IDC_MON_PATH3
    };
    static const int active_ids[MONITOR_MAX_PATHS] = {
        IDC_MON_ACTIVE1, IDC_MON_ACTIVE2, IDC_MON_ACTIVE3
    };
    static const int browse_ids[MONITOR_MAX_PATHS] = {
        IDC_MON_BROWSE1, IDC_MON_BROWSE2, IDC_MON_BROWSE3
    };
    int i;

    (void)lparam;
    if (message == WM_INITDIALOG) {
        monitor_config_t configs[MONITOR_MAX_PATHS];
        Monitor_GetConfigs(configs);
        for (i = 0; i < MONITOR_MAX_PATHS; i++) {
            SetDlgItemTextA(dialog, path_ids[i], configs[i].path);
            CheckDlgButton(dialog, active_ids[i],
                           configs[i].is_active ? BST_CHECKED : BST_UNCHECKED);
        }
        return TRUE;
    }
    if (message != WM_COMMAND)
        return FALSE;
    for (i = 0; i < MONITOR_MAX_PATHS; i++) {
        if (LOWORD(wparam) == browse_ids[i]) {
            browse_for_folder(dialog, i);
            return TRUE;
        }
    }
    if (LOWORD(wparam) == IDOK) {
        monitor_config_t configs[MONITOR_MAX_PATHS];
        ZeroMemory(configs, sizeof(configs));
        for (i = 0; i < MONITOR_MAX_PATHS; i++) {
            GetDlgItemTextA(dialog, path_ids[i], configs[i].path, MAX_PATH);
            configs[i].is_active =
                IsDlgButtonChecked(dialog, active_ids[i]) == BST_CHECKED;
            if (configs[i].is_active) {
                DWORD attributes = GetFileAttributesA(configs[i].path);
                if (!configs[i].path[0] ||
                    attributes == INVALID_FILE_ATTRIBUTES ||
                    !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    MessageBoxA(dialog, "Each enabled monitor path must be an existing folder.",
                                "Folder Monitor Settings", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(dialog, path_ids[i]));
                    return TRUE;
                }
            }
        }
        if (!Monitor_UpdateConfigs(configs)) {
            MessageBoxA(dialog, "Could not save monitor settings or start the monitor.",
                        "Folder Monitor Settings", MB_OK | MB_ICONERROR);
            return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (LOWORD(wparam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void Monitor_ShowSettingsDialog(HWND hwnd_parent)
{
    if (DialogBoxParamA(GetModuleHandleA(NULL),
                        MAKEINTRESOURCEA(IDD_MONITOR_SETTINGS), hwnd_parent,
                        monitor_settings_proc, 0) == -1)
        MessageBoxA(hwnd_parent, "Could not open the monitor settings dialog.",
                    "Folder Monitor Settings", MB_OK | MB_ICONERROR);
}
