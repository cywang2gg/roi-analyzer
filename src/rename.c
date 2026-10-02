#include "rename.h"

#include <stdio.h>
#include <string.h>
#include <shlwapi.h>

static void set_error(char *buffer, size_t cap, const char *message)
{
    if (!buffer || cap == 0)
        return;
    strncpy(buffer, message, cap - 1);
    buffer[cap - 1] = '\0';
}

static BOOL is_reserved_base(const char *base, size_t len)
{
    static const char *const reserved[] = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
        "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    size_t i;

    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        size_t reserved_length = strlen(reserved[i]);
        if (len == reserved_length &&
            _strnicmp(base, reserved[i], reserved_length) == 0)
            return TRUE;
    }
    return FALSE;
}

BOOL ExtractPrefix(const char *new_name, const char *orig_name,
                   char *prefix, size_t cap)
{
    size_t new_length, orig_length, prefix_length;
    const char *match;

    if (!prefix || cap == 0)
        return FALSE;
    prefix[0] = '\0';
    if (!new_name || !orig_name)
        return FALSE;
    new_length = strlen(new_name);
    orig_length = strlen(orig_name);
    if (new_length == 0 || orig_length == 0)
        return FALSE;
    if (new_length >= orig_length &&
        strcmp(new_name + new_length - orig_length, orig_name) == 0) {
        prefix_length = new_length - orig_length;
        if (prefix_length >= cap)
            prefix_length = cap - 1;
        memcpy(prefix, new_name, prefix_length);
        prefix[prefix_length] = '\0';
        return prefix_length != 0;
    }
    match = strstr(new_name, orig_name);
    if (match && match > new_name) {
        prefix_length = (size_t)(match - new_name);
        if (prefix_length >= cap)
            prefix_length = cap - 1;
        memcpy(prefix, new_name, prefix_length);
        prefix[prefix_length] = '\0';
        return prefix_length != 0;
    }
    prefix_length = new_length < cap - 1 ? new_length : cap - 1;
    memcpy(prefix, new_name, prefix_length);
    prefix[prefix_length] = '\0';
    return prefix_length != 0;
}

BOOL Rename_ValidateFileName(const char *file_name, char *err_msg,
                             size_t err_cap)
{
    static const char invalid_chars[] = "<>:\"/\\|?*";
    const char *p, *dot;
    size_t length, base_length;

    if (err_msg && err_cap)
        err_msg[0] = '\0';
    if (!file_name || !file_name[0]) {
        set_error(err_msg, err_cap, "File name cannot be empty.");
        return FALSE;
    }
    length = strlen(file_name);
    if (length > (size_t)(MAX_PATH - 32)) {
        set_error(err_msg, err_cap, "File name exceeds maximum length.");
        return FALSE;
    }
    for (p = file_name; *p; p++) {
        if ((unsigned char)*p < 0x20 || strchr(invalid_chars, *p)) {
            set_error(err_msg, err_cap,
                      "File name contains an invalid character.");
            return FALSE;
        }
    }
    if (file_name[length - 1] == ' ' || file_name[length - 1] == '.') {
        set_error(err_msg, err_cap,
                  "File name cannot end with a space or dot.");
        return FALSE;
    }
    dot = strchr(file_name, '.');
    base_length = dot ? (size_t)(dot - file_name) : length;
    while (base_length && (file_name[base_length - 1] == ' ' ||
                           file_name[base_length - 1] == '.'))
        base_length--;
    if (is_reserved_base(file_name, base_length)) {
        set_error(err_msg, err_cap, "File name is a reserved device name.");
        return FALSE;
    }
    return TRUE;
}

BOOL Rename_IsSupportedExtension(const char *file_path)
{
    const char *extension;
    if (!file_path || !file_path[0])
        return FALSE;
    extension = PathFindExtensionA(file_path);
    return extension &&
           (_stricmp(extension, ".png") == 0 ||
            _stricmp(extension, ".jpg") == 0 ||
            _stricmp(extension, ".jpeg") == 0 ||
            _stricmp(extension, ".bmp") == 0);
}

static DWORD move_overwrite_target(const char *target, char *backup,
                                   size_t backup_cap)
{
    unsigned int conflict;
    char candidate[MAX_PATH];
    int written;
    DWORD error;

    for (conflict = 0; conflict < 10000; conflict++) {
        if (conflict == 0)
            written = _snprintf(candidate, sizeof(candidate), "%s.bak", target);
        else
            written = _snprintf(candidate, sizeof(candidate),
                                "%s_conflict_%u.bak", target, conflict);
        if (written < 0 || (size_t)written >= sizeof(candidate))
            return ERROR_FILENAME_EXCED_RANGE;
        if ((size_t)written >= backup_cap)
            return ERROR_BUFFER_OVERFLOW;
        if (MoveFileExA(target, candidate, MOVEFILE_COPY_ALLOWED)) {
            memcpy(backup, candidate, (size_t)written + 1);
            return ERROR_SUCCESS;
        }
        error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            return error;
    }
    return ERROR_FILE_EXISTS;
}

static void move_related_csv_files(const char *old_path, const char *new_path)
{
    static const char *const modes[] = {
        "drag", "grid3x3", "grid5x5"
    };
    char old_dir[MAX_PATH], new_dir[MAX_PATH];
    const char *old_file, *new_file, *old_ext, *new_ext;
    size_t old_base_length, new_base_length;
    BOOL warned = FALSE;
    int i;

    old_file = PathFindFileNameA(old_path);
    new_file = PathFindFileNameA(new_path);
    old_ext = PathFindExtensionA(old_file);
    new_ext = PathFindExtensionA(new_file);
    old_base_length = old_ext ? (size_t)(old_ext - old_file) :
                                strlen(old_file);
    new_base_length = new_ext ? (size_t)(new_ext - new_file) :
                                strlen(new_file);
    strncpy(old_dir, old_path, sizeof(old_dir) - 1);
    old_dir[sizeof(old_dir) - 1] = '\0';
    strncpy(new_dir, new_path, sizeof(new_dir) - 1);
    new_dir[sizeof(new_dir) - 1] = '\0';
    if (!PathRemoveFileSpecA(old_dir) || !PathRemoveFileSpecA(new_dir))
        return;
    for (i = 0; i < 3; i++) {
        char source[MAX_PATH], destination[MAX_PATH];
        int source_length, destination_length;
        source_length = _snprintf(source, sizeof(source), "%s\\%.*s_%s.csv",
                                  old_dir, (int)old_base_length, old_file,
                                  modes[i]);
        destination_length =
            _snprintf(destination, sizeof(destination), "%s\\%.*s_%s.csv",
                      new_dir, (int)new_base_length, new_file, modes[i]);
        if (source_length < 0 || source_length >= (int)sizeof(source) ||
            destination_length < 0 ||
            destination_length >= (int)sizeof(destination))
            continue;
        if (GetFileAttributesA(source) != INVALID_FILE_ATTRIBUTES &&
            !MoveFileExA(source, destination,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            OutputDebugStringA("ROI Analyzer: could not move a related ROI CSV file.\n");
            warned = TRUE;
        }
    }
    if (warned)
        MessageBoxA(GetActiveWindow(),
                    "The image was renamed, but one or more related ROI CSV files could not be renamed.",
                    "Rename Image", MB_OK | MB_ICONWARNING);
}

DWORD Rename_Execute(const char *old_path, const char *new_name,
                     char *out_new_path, size_t cap, BOOL overwrite)
{
    char full_old_path[MAX_PATH], target[MAX_PATH], backup[MAX_PATH];
    const char *extension;
    char error[128];
    size_t directory_length;
    DWORD full_path_length;
    int written;
    BOOL backed_up = FALSE;
    DWORD result;
    DWORD target_attributes;

    if (!old_path || !old_path[0] || !new_name || !out_new_path ||
        cap == 0 || cap > MAXDWORD)
        return ERROR_INVALID_PARAMETER;
    out_new_path[0] = '\0';
    if (!Rename_ValidateFileName(new_name, error, sizeof(error)))
        return ERROR_INVALID_NAME;
    if (GetFileAttributesA(old_path) == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    full_path_length = GetFullPathNameA(old_path, MAX_PATH, full_old_path, NULL);
    if (full_path_length == 0)
        return GetLastError();
    if (full_path_length >= (DWORD)MAX_PATH)
        return ERROR_FILENAME_EXCED_RANGE;
    extension = PathFindExtensionA(full_old_path);
    if (!extension)
        extension = full_old_path + strlen(full_old_path);
    strncpy(target, full_old_path, sizeof(target) - 1);
    target[sizeof(target) - 1] = '\0';
    if (!PathRemoveFileSpecA(target))
        return ERROR_INVALID_NAME;
    directory_length = strlen(target);
    written = _snprintf(target + directory_length,
                        sizeof(target) - directory_length,
                        "%s%s%s%s",
                        directory_length && target[directory_length - 1] != '\\' &&
                        target[directory_length - 1] != '/' ? "\\" : "",
                        new_name, "", extension);
    if (written < 0 ||
        (size_t)written >= sizeof(target) - directory_length)
        return ERROR_FILENAME_EXCED_RANGE;
    if (strlen(target) >= cap)
        return ERROR_INSUFFICIENT_BUFFER;
    if (_stricmp(full_old_path, target) == 0)
        return ERROR_INVALID_PARAMETER;

    target_attributes = GetFileAttributesA(target);
    if (target_attributes != INVALID_FILE_ATTRIBUTES) {
        if (target_attributes & FILE_ATTRIBUTE_DIRECTORY)
            return ERROR_ACCESS_DENIED;
        if (!overwrite)
            return ERROR_ALREADY_EXISTS;
        result = move_overwrite_target(target, backup, sizeof(backup));
        if (result != ERROR_SUCCESS)
            return result;
        backed_up = TRUE;
    }
    if (!MoveFileExA(old_path, target,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        result = GetLastError();
        if (backed_up &&
            !MoveFileExA(backup, target,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            OutputDebugStringA("ROI Analyzer: could not restore overwrite backup after rename failure.\n");
            MessageBoxA(GetActiveWindow(),
                        "The image rename failed, and the original target could not be restored. Its backup remains on disk.",
                        "Rename Image", MB_OK | MB_ICONERROR);
        }
        return result;
    }
    memcpy(out_new_path, target, strlen(target) + 1);
    move_related_csv_files(full_old_path, target);
    return ERROR_SUCCESS;
}
