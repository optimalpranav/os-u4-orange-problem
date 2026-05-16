// index.c — Staging area (index) management for PES-VCS
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

static int compare_entries(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path, ((const IndexEntry *)b)->path);
}

IndexEntry *index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++)
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    return NULL;
}

int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;
    char line[800];
    while (fgets(line, sizeof(line), f)) {
        size_t ln = strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r')) line[--ln] = '\0';
        if (ln == 0) continue;
        if (index->count >= MAX_INDEX_ENTRIES) break;
        IndexEntry *e = &index->entries[index->count];
        unsigned int mode, sz;
        unsigned long long mtime = 0;
        char hex[HASH_HEX_SIZE + 2];
        char path[512];
        if (sscanf(line, "%o %65s %llu %u %511[^\n]", &mode, hex, &mtime, &sz, path) != 5) continue;
        e->mode = (uint32_t)mode;
        e->mtime_sec = (uint64_t)mtime;
        e->size = (uint32_t)sz;
        strncpy(e->path, path, sizeof(e->path) - 1);
        e->path[sizeof(e->path) - 1] = '\0';
        if (hex_to_hash(hex, &e->hash) != 0) continue;
        index->count++;
    }
    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    Index sorted = *index;
    qsort(sorted.entries, sorted.count, sizeof(IndexEntry), compare_entries);
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", INDEX_FILE);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    for (int i = 0; i < sorted.count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted.entries[i].hash, hex);
        fprintf(f, "%o %s %llu %u %s\n",
                sorted.entries[i].mode, hex,
                (unsigned long long)sorted.entries[i].mtime_sec,
                sorted.entries[i].size, sorted.entries[i].path);
    }
    fflush(f); fsync(fileno(f)); fclose(f);
    return rename(tmp, INDEX_FILE);
}

int index_add(Index *index, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);
    void *contents = NULL;
    if (file_size > 0) {
        contents = malloc((size_t)file_size);
        if (!contents) { fclose(f); return -1; }
        if ((long)fread(contents, 1, (size_t)file_size, f) != file_size) {
            free(contents); fclose(f); return -1;
        }
    }
    fclose(f);
    ObjectID blob_id;
    int rc = object_write(OBJ_BLOB,
                          contents ? contents : (void *)"",
                          (size_t)(file_size > 0 ? file_size : 0),
                          &blob_id);
    free(contents);
    if (rc != 0) return -1;
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    uint32_t mode = (st.st_mode & S_IXUSR) ? 0100755 : 0100644;
    IndexEntry *existing = index_find(index, path);
    if (existing) {
        existing->hash = blob_id; existing->mode = mode;
        existing->mtime_sec = (uint64_t)st.st_mtime; existing->size = (uint32_t)st.st_size;
    } else {
        if (index->count >= MAX_INDEX_ENTRIES) { fprintf(stderr, "error: index is full\n"); return -1; }
        IndexEntry *e = &index->entries[index->count++];
        e->hash = blob_id; e->mode = mode;
        e->mtime_sec = (uint64_t)st.st_mtime; e->size = (uint32_t)st.st_size;
        strncpy(e->path, path, sizeof(e->path) - 1);
        e->path[sizeof(e->path) - 1] = '\0';
    }
    return index_save(index);
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i+1],
                        (size_t)remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    int sc = 0;
    for (int i = 0; i < index->count; i++) { printf("  staged:     %s\n", index->entries[i].path); sc++; }
    if (sc == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    int uc = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path); uc++;
        } else if (st.st_mtime != (time_t)index->entries[i].mtime_sec ||
                   (uint32_t)st.st_size != index->entries[i].size) {
            printf("  modified:   %s\n", index->entries[i].path); uc++;
        }
    }
    if (uc == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    int tc = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;
            const char *dot = strrchr(ent->d_name, '.');
            if (dot && strcmp(dot, ".o") == 0) continue;
            int tracked = 0;
            for (int i = 0; i < index->count; i++)
                if (strcmp(index->entries[i].path, ent->d_name) == 0) { tracked = 1; break; }
            if (tracked) continue;
            struct stat st;
            if (stat(ent->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                printf("  untracked:  %s\n", ent->d_name); tc++;
            }
        }
        closedir(dir);
    }
    if (tc == 0) printf("  (nothing to show)\n");
    printf("\n");
    return 0;
}
