#ifndef ROI_MONITOR_H
#define ROI_MONITOR_H

#include <windows.h>
#include <stddef.h>

#define MONITOR_MAX_PATHS 3
#define WM_APP_NEW_FILE   (WM_APP + 101)

typedef struct {
    char path[MAX_PATH];
    BOOL is_active;
} monitor_config_t;

typedef enum {
    MONITOR_STATUS_ACTIVE,
    MONITOR_STATUS_STOPPED,
    MONITOR_STATUS_UNSET
} monitor_status_t;

BOOL Monitor_Init(HWND hwnd_notify);
void Monitor_Shutdown(void);
BOOL Monitor_UpdateConfigs(const monitor_config_t configs[MONITOR_MAX_PATHS]);
void Monitor_GetConfigs(monitor_config_t configs[MONITOR_MAX_PATHS]);
monitor_status_t Monitor_GetStatus(int index, char *out_path, size_t cap);
void Monitor_StartAll(void);
void Monitor_StopAll(void);
void Monitor_NoteSelfRename(const char *abs_path);
BOOL Monitor_IsSelfRename(const char *path);
BOOL File_WaitForWriteComplete(const char *path, DWORD timeout_ms);
void Monitor_ShowSettingsDialog(HWND hwnd_parent);

#endif
