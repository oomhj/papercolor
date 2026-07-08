/**
 * PaperColor — Simple Key=Value Config File Helpers
 *
 * Read/write key=value pairs in a text file on FatFS/SD.
 */

#include "config_file.h"
#include <cstdio>
#include <cstring>

bool config_read_val(const char* path, const char* key, char* val, size_t val_sz)
{
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    size_t klen = strlen(key);
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            size_t vlen = strlen(line + klen + 1);
            if (vlen >= val_sz) vlen = val_sz - 1;
            memcpy(val, line + klen + 1, vlen);
            val[vlen] = '\0';
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

void config_write_val(const char* path, const char* key, const char* val)
{
    // Read existing file
    char tmp[512] = {};
    FILE* f = fopen(path, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f))
            strncat(tmp, line, sizeof(tmp) - strlen(tmp) - 1);
        fclose(f);
    }
    // Remove old line with matching key (match "key=" at line start)
    size_t klen = strlen(key);
    char* p = tmp;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == tmp || *(p - 1) == '\n') && *(p + klen) == '=') {
            char* nl = strchr(p, '\n');
            if (nl) memmove(p, nl + 1, strlen(nl + 1) + 1);
            else *p = '\0';
            break;
        }
        p++;
    }
    // Append new
    char newline[128];
    snprintf(newline, sizeof(newline), "%s=%s\n", key, val);
    strncat(tmp, newline, sizeof(tmp) - strlen(tmp) - 1);
    // Write directly (FatFS f_rename doesn't overwrite; direct write is reliable)
    f = fopen(path, "w");
    if (f) { fputs(tmp, f); fclose(f); }
}
