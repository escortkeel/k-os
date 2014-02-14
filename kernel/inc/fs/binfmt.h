#ifndef KERNEL_FS_BINFMT_H
#define KERNEL_FS_BINFMT_H

#include "lib/int.h"
#include "common/list.h"

typedef struct binfmt {
    list_head_t list;

    int (*load_exe)(const char *name, void *start, uint32_t length);
} binfmt_t;

void binfmt_register(binfmt_t *binfmt);

int binfmt_load_exe(const char *name, void *start, uint32_t length);

#endif
