#define _DEFAULT_SOURCE

#include "directories.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static bool should_ignore_entry(const char *name)
{
    return strcmp(name, ".") == 0 ||
           strcmp(name, "..") == 0;
}

static EdensDirectoryEntryType get_entry_type(unsigned char type)
{
    switch (type) {
        case DT_REG:
            return EDENS_ENTRY_FILE;

        case DT_DIR:
            return EDENS_ENTRY_DIRECTORY;

        case DT_LNK:
            return EDENS_ENTRY_SYMLINK;

        default:
            return EDENS_ENTRY_UNKNOWN;
    }
}

void free_directory_entries(EdensDirectoryEntries *entries)
{
    if (entries == NULL) {
        return;
    }

    for (size_t i = 0; i < entries->count; i++) {
        free(entries->entries[i].name);
    }

    free(entries->entries);

    entries->entries = NULL;
    entries->count = 0;
}

int read_directory(const char *path, EdensDirectoryEntries *result)
{
    if (path == NULL || result == NULL) {
        return EINVAL;
    }

    result->entries = NULL;
    result->count = 0;

    DIR *directory = opendir(path);

    if (directory == NULL) {
        return errno;
    }

    size_t capacity = 16;

    result->entries = malloc(
        capacity * sizeof(EdensDirectoryEntry)
    );

    if (result->entries == NULL) {
        closedir(directory);
        return ENOMEM;
    }

    struct dirent *entry;

    errno = 0;

    while ((entry = readdir(directory)) != NULL) {
        if (should_ignore_entry(entry->d_name)) {
            continue;
        }

        if (result->count == capacity) {
            capacity *= 2;

            EdensDirectoryEntry *new_entries = realloc(
                result->entries,
                capacity * sizeof(EdensDirectoryEntry)
            );

            if (new_entries == NULL) {
                closedir(directory);
                free_directory_entries(result);
                return ENOMEM;
            }

            result->entries = new_entries;
        }

        char *name = strdup(entry->d_name);

        if (name == NULL) {
            closedir(directory);
            free_directory_entries(result);
            return ENOMEM;
        }

        result->entries[result->count].name = name;
        result->entries[result->count].type =
            get_entry_type(entry->d_type);

        result->count++;
    }

    int read_error = errno;
    closedir(directory);

    if (read_error != 0) {
        free_directory_entries(result);
        return read_error;
    }

    return 0;
}
