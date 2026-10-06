// viz_bend.c is generated, and its whole text keys its GPU program (viz.gpu), so it is never edited:
// what macOS lacks comes in through this header, included ahead of it in macOS builds. Its Vulkan
// heap is two views of one anonymous shared file, made with memfd_create; on macOS that file is a
// shared memory object, unlinked at once.
#pragma once
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

static inline int memfd_create(const char *name, unsigned flags) {
    (void)name, (void)flags;
    char shm[32];
    snprintf(shm, sizeof shm, "/bend-%d", (int)getpid());
    int fd = shm_open(shm, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) shm_unlink(shm);
    return fd;
}
