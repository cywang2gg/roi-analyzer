#ifndef ROI_SETTINGS_H
#define ROI_SETTINGS_H

#include <windows.h>
#include <stddef.h>

#include "monitor.h"

typedef struct {
    BOOL enabled;
    BOOL show_grid;
} locate_options_t;

void Settings_GetIniPath(char *path, size_t cap);
BOOL Settings_LoadMonitorConfigs(monitor_config_t configs[MONITOR_MAX_PATHS]);
BOOL Settings_SaveMonitorConfigs(const monitor_config_t configs[MONITOR_MAX_PATHS]);
BOOL Settings_LoadLastRenamePrefix(char *prefix, size_t cap);
BOOL Settings_SaveLastRenamePrefix(const char *prefix);
BOOL Settings_LoadLocateOptions(locate_options_t *options);
BOOL Settings_SaveLocateOptions(const locate_options_t *options);

#endif
