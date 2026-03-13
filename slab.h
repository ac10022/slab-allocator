#ifndef SLAB_H
#define SLAB_H

#include <stdbool.h>

typedef struct S_Slab S_Slab;

typedef struct {
    unsigned int object_size;
    unsigned int order;          // we follow Linux slab orderings, i.e. order of 0 is 1 page (4Kb), then order of  1 is 1 * 2^1 pages (8Kb) etc.
    
    S_Slab* partial_list;
    S_Slab* full_list;
    S_Slab* free_list;
} S_Cache;

void S_CacheSimpleInit(S_Cache* cache, unsigned int size);
void S_CacheInit(S_Cache* cache, unsigned int size, unsigned int order);
void* S_SlabAlloc(S_Cache* cache);
void S_SlabFree(S_Cache* cache, void* ptr);
void S_CacheDestroy(S_Cache* cache);

#endif