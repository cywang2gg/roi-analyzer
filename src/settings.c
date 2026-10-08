#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <shlwapi.h>

void Settings_GetIniPath(char *path, size_t cap)
{
    DWORD length;

    if (!path || cap == 0 || cap > MAXDWORD) {
        return;
    }
    path[0] = '\0';
    length = GetModuleFileNameA(NULL, path, (DWORD)cap);
    if (length == 0 || length >= cap ||
        !PathRemoveFileSpecA(path) ||
        !PathAppendA(path, "roi_analyzer.ini")) {
        path[0] = '\0';
    }
}

BOOL Settings_LoadMonitorConfigs(monitor_config_t configs[MONITOR_MAX_PATHS])
{
    char ini[MAX_PATH], key[32];
    int i;

    if (!configs)
        return FALSE;
    ZeroMemory(configs, sizeof(monitor_config_t) * MONITOR_MAX_PATHS);
    Settings_GetIniPath(ini, sizeof(ini));
    if (!ini[0])
        return FALSE;
    for (i = 0; i < MONITOR_MAX_PATHS; i++) {
        _snprintf(key, sizeof(key), "Path%d", i);
        key[sizeof(key) - 1] = '\0';
        GetPrivateProfileStringA("Monitor", key, "", configs[i].path,
                                 MAX_PATH, ini);
        _snprintf(key, sizeof(key), "Active%d", i);
        key[sizeof(key) - 1] = '\0';
        configs[i].is_active =
            GetPrivateProfileIntA("Monitor", key, 0, ini) != 0;
    }
    return TRUE;
}

BOOL Settings_SaveMonitorConfigs(const monitor_config_t configs[MONITOR_MAX_PATHS])
{
    char ini[MAX_PATH], key[32], value[16];
    int i;

    if (!configs)
        return FALSE;
    Settings_GetIniPath(ini, sizeof(ini));
    if (!ini[0])
        return FALSE;
    for (i = 0; i < MONITOR_MAX_PATHS; i++) {
        _snprintf(key, sizeof(key), "Path%d", i);
        key[sizeof(key) - 1] = '\0';
        if (!WritePrivateProfileStringA("Monitor", key, configs[i].path, ini))
            return FALSE;
        _snprintf(key, sizeof(key), "Active%d", i);
        key[sizeof(key) - 1] = '\0';
        _snprintf(value, sizeof(value), "%d", configs[i].is_active ? 1 : 0);
        value[sizeof(value) - 1] = '\0';
        if (!WritePrivateProfileStringA("Monitor", key, value, ini))
            return FALSE;
    }
    return TRUE;
}

BOOL Settings_LoadLastRenamePrefix(char *prefix, size_t cap)
{
    char ini[MAX_PATH];
    DWORD length;

    if (!prefix || cap == 0 || cap > MAXDWORD)
        return FALSE;
    prefix[0] = '\0';
    Settings_GetIniPath(ini, sizeof(ini));
    if (!ini[0])
        return FALSE;
    length = GetPrivateProfileStringA("Rename", "LastRenamePrefix", "",
                                      prefix, (DWORD)cap, ini);
    return length > 0;
}

BOOL Settings_SaveLastRenamePrefix(const char *prefix)
{
    char ini[MAX_PATH];

    if (!prefix)
        return FALSE;
    Settings_GetIniPath(ini, sizeof(ini));
    return ini[0] &&
           WritePrivateProfileStringA("Rename", "LastRenamePrefix", prefix, ini);
}

BOOL Settings_LoadLocateOptions(locate_options_t *options)
{
    char ini[MAX_PATH];

    if (options == NULL)
        return FALSE;
    options->enabled = TRUE;
    options->show_grid = TRUE;
    Settings_GetIniPath(ini, sizeof(ini));
    if (ini[0] == '\0')
        return FALSE;
    options->enabled =
        GetPrivateProfileIntA("locate", "enabled", 1, ini) != 0;
    options->show_grid =
        GetPrivateProfileIntA("locate", "show_grid", 1, ini) != 0;
    return TRUE;
}

BOOL Settings_SaveLocateOptions(const locate_options_t *options)
{
    char ini[MAX_PATH];
    const char *enabled;
    const char *show_grid;

    if (options == NULL)
        return FALSE;
    Settings_GetIniPath(ini, sizeof(ini));
    if (ini[0] == '\0')
        return FALSE;
    enabled = options->enabled ? "1" : "0";
    show_grid = options->show_grid ? "1" : "0";
    return WritePrivateProfileStringA("locate", "enabled", enabled, ini) &&
           WritePrivateProfileStringA("locate", "show_grid", show_grid, ini);
}

BOOL Settings_LoadHistWidth(int *logical_width)
{
    char ini[MAX_PATH];

    if (!logical_width)
        return FALSE;
    *logical_width = 0;
    Settings_GetIniPath(ini, sizeof(ini));
    if (ini[0] == '\0')
        return FALSE;
    *logical_width = GetPrivateProfileIntA("Layout", "HistWidth", 0, ini);
    return TRUE;
}

BOOL Settings_SaveHistWidth(int logical_width)
{
    char ini[MAX_PATH];
    char value[16];

    Settings_GetIniPath(ini, sizeof(ini));
    if (ini[0] == '\0')
        return FALSE;
    _snprintf(value, sizeof(value), "%d", logical_width);
    value[sizeof(value) - 1] = '\0';
    return WritePrivateProfileStringA("Layout", "HistWidth", value, ini);
}
