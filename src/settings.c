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
