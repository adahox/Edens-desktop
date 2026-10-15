#ifndef EDENS_DIRECTORIES_H
#define EDENS_DIRECTORIES_H

#include <stddef.h>

typedef enum {
    EDENS_ENTRY_FILE,
    EDENS_ENTRY_DIRECTORY,
    EDENS_ENTRY_SYMLINK,
    EDENS_ENTRY_UNKNOWN
} EdensDirectoryEntryType;

typedef struct {
    char *name;
    EdensDirectoryEntryType type;
} EdensDirectoryEntry;

typedef struct {
    EdensDirectoryEntry *entries;
    size_t count;
} EdensDirectoryEntries;

int read_directory(
    const char *path,
    EdensDirectoryEntries *result
);

void free_directory_entries(
    EdensDirectoryEntries *entries
);

#endif