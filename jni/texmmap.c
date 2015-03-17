#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

struct texmmap {
    int fd;
    uint64_t file_size;
    void *mmap_ptr;
};

struct texmmap texmmap_;

int texmmap_open(const char *dir, const char *filename, struct texmmap* texmmap) {
    memset(texmmap, 0, sizeof(struct texmmap));

    size_t path_size = strlen(dir) + strlen(filename) + 2;
    char filepath[path_size];

    snprintf(filepath, path_size, "%s/%s", dir, filename);

    int fd = open(filepath, O_RDONLY);
    if(fd < 0) {
        LOGW("Can't open %s: %d\n", filepath, errno);
        return -1;
    }

    struct stat statbuf;
    if(fstat(fd, &statbuf) != 0) {
        LOGW("Can't stat %s: %d\n", filepath, errno);
        close(fd);
        return -1;
    }

    uint64_t file_size = statbuf.st_size;
    uint64_t offset = 0;

    int mmap_flags = MAP_PRIVATE; // | MAP_HUGETLB
    void *mmap_ptr = 0;
    if((mmap_ptr = mmap(&mmap_ptr, file_size, PROT_READ, mmap_flags, fd, offset))
        == MAP_FAILED) {
        LOGW("Can't mmap %s: %d\n", filepath, errno);
        close(fd);
        return -1;
    }

    texmmap->fd = fd;
    texmmap->file_size = file_size;
    texmmap->mmap_ptr = mmap_ptr;

    return 0;
}

int texmmap_close(struct texmmap* texmmap) {
    munmap(texmmap->mmap_ptr, texmmap->file_size);
    close(texmmap->fd);

    return 0;
}

void *texmmap_ptr(const struct texmmap *texmmap) {
    return texmmap->mmap_ptr;
}

uint64_t texmmap_size(const struct texmmap *texmmap) {
    return texmmap->file_size;
}
