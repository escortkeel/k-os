#ifndef KERNEL_CACHE_H
#define KERNEL_CACHE_H

#include "int.h"
#include "init.h"

typedef struct cache cache_t;

cache_t * cache_create(uint32_t size);
void * cache_alloc(cache_t *cache);
void cache_free(cache_t *cache, void *mem);

void * kmalloc(uint32_t size);
void kfree(void *mem, uint32_t size);

void cache_init();

#endif