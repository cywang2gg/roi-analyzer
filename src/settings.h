#ifndef ROI_SETTINGS_H
#define ROI_SETTINGS_H

#include <windows.h>
#include <stddef.h>

#include "monitor.h"

void Settings_GetIniPath(char *path, size_t cap);
BOOL Settings_LoadMonitorConfigs(monitor_config_t configs[MONITOR_MAX_PATHS]);
BOOL Settings_SaveMonitorConfigs(const monitor_config_t configs[MONITOR_MAX_PATHS]);
BOOL Settings_LoadLastRenamePrefix(char *prefix, size_t cap);
BOOL Settings_SaveLastRenamePrefix(const char *prefix);

#endif
