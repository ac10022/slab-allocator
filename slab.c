#include "slab.h"

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#define PAGE_SIZE       4096
#define FULL_BITMASK    0xFFFFFFFFFFFFFFFF
#define SLAB_FULL       0xFFFFFFFF

typedef unsigned long   u64;
typedef unsigned int    u32;
typedef int             i32;

struct S_Slab {
    void* data;
    u64* bitmasks;
    u32 mask_count;
    S_Slab* next;
};

/*
 * Initialise cache for a specific struct, size of this struct indicated by the u32 size paramater.
 */
void S_CacheInit(S_Cache* cache, u32 size, u32 order) {
    if (size % sizeof(void*) != 0) {
        size += sizeof(void*) - (size % sizeof(void*));
    }
    
    cache->object_size = size;
    cache->order = order;

    cache->partial_list = NULL;
    cache->free_list = NULL;
    cache->full_list = NULL;
}

/*
 * Get the first slot of the slab are free for allocation
 */
u32 S_GetFirstFreeIndexAndMark(S_Slab* partial_slab) {
    u32 bitmask_number = 0;

    // find the first bitmask which is not full
    while (partial_slab->bitmasks[bitmask_number] == FULL_BITMASK) {
        // if we reach the last bitmask and it is full, then the whole slab is full
        if (bitmask_number == partial_slab->mask_count - 1) return SLAB_FULL;
        bitmask_number++;
    } 

    // high-speed CPU instruction to get the index of first 0 bit, i.e. free slot index 
    u32 index = (u32)__builtin_ctzl(~partial_slab->bitmasks[bitmask_number]);

    // mark slot as used
    partial_slab->bitmasks[bitmask_number] |= ((u64)1 << index);

    return (bitmask_number << 6) + index;
}

void S_MarkInvalidSlotsAsUsed(S_Slab* new_slab, u32 total_slots) {
    if ((new_slab->mask_count << 6) == total_slots) return; // perfectly fits

    u32 invalid_mask_start = total_slots >> 6;
    u32 invalid_mask_index_start = total_slots % 64;

    for (u32 i = invalid_mask_index_start; i < 64; i++) {
        new_slab->bitmasks[invalid_mask_start] |= ((u64)1 << i);
    }
    
    for (u32 i = invalid_mask_start + 1; i < new_slab->mask_count; i++) {
        new_slab->bitmasks[i] = FULL_BITMASK;
    }
}

S_Slab* S_CreateNewSlab(S_Cache* cache) {
    S_Slab* new_slab = (S_Slab*)calloc(1, sizeof(S_Slab));
    u64 slab_memory_size = PAGE_SIZE << cache->order;

    // align for page alignment
    i32 res = posix_memalign(&new_slab->data, PAGE_SIZE, slab_memory_size);
    if (res != 0) {
        // TODO: alignment error
    }

    u32 total_slots = slab_memory_size / cache->object_size;

    // how many 64 bit bitmasks we will need to accommodate all struct slots
    new_slab->mask_count = (total_slots + 63) / 64;
    new_slab->bitmasks = calloc(new_slab->mask_count, sizeof(u64));
    S_MarkInvalidSlotsAsUsed(new_slab, total_slots);

    return new_slab;
}

bool S_CheckSlabFull(S_Slab* slab) {
    for (u32 i = 0; i < slab->mask_count; i++) {
        if (slab->bitmasks[i] != FULL_BITMASK) return false;    
    }
    return true;
}

bool S_CheckSlabEmpty(S_Cache* cache, S_Slab* slab) {
    u32 total_slots_used = (PAGE_SIZE << cache->order) / cache->object_size; // how many slots were actually used based on the user defined struct size 
    u32 masks_used_fully = (total_slots_used >> 6); // how many masks should have been used fully based on the total slots used

    for (u32 i = 0; i < masks_used_fully; i++) {
        if (slab->bitmasks[i] != 0) return false;
    }

    u32 remaining_bits_to_check = total_slots_used % 64;
    if (remaining_bits_to_check == 0) return true;

    u64 mask = ((u64)1 << remaining_bits_to_check) - 1; // sets all bits which should be checked to 1
    // if these bits are not all 0, the slab is not completely empty
    if ((slab->bitmasks[masks_used_fully] & mask) != 0) return false;

    return true;
}

void S_AddSlabToCachePartialList(S_Cache* cache, S_Slab* slab) {
    slab->next = cache->partial_list;
    cache->partial_list = slab;
}

void S_AddSlabToCacheFullList(S_Cache* cache, S_Slab* slab) {
    slab->next = cache->full_list;
    cache->full_list = slab;
}

void S_AddSlabToCacheFreeList(S_Cache* cache, S_Slab* slab) {
    slab->next = cache->free_list;
    cache->free_list = slab;
}

void* S_SlabAlloc(S_Cache* cache) {
    if (cache->partial_list == NULL) { // we need to create a partial slab
        // check free list is empty, if so, use a slab from the free list first before creating a new one
        if (cache->free_list) {
            S_Slab* slab = cache->free_list;
            cache->free_list = slab->next;
            S_AddSlabToCachePartialList(cache, slab);
        } else {
            S_Slab* new_slab = S_CreateNewSlab(cache);
            S_AddSlabToCachePartialList(cache, new_slab);
        }
    }

    S_Slab* partial_slab = cache->partial_list; 
    u32 global_index = S_GetFirstFreeIndexAndMark(partial_slab);
    
    if (global_index == SLAB_FULL) {
        // move from partial list to full list
        cache->partial_list = partial_slab->next;
        S_AddSlabToCacheFullList(cache, partial_slab);

        return S_SlabAlloc(cache); // retry with next slab
    }

    return (char*)partial_slab->data + (global_index * cache->object_size);
}

void S_SlabFree(S_Cache* cache, void* ptr) {
    if (!ptr) return; // called with null
    
    bool located_in_full = false;
    u64 slab_bytes = (u64)PAGE_SIZE << cache->order;
    
    // check partial list for the slab which contains our pointer
    S_Slab* target_slab = cache->partial_list;
    S_Slab* prev_slab = NULL;
    while (target_slab) {
        if (ptr >= target_slab->data 
            && ptr < (void*)((char*)target_slab->data + slab_bytes)) {
            break;
        }
        prev_slab = target_slab;
        target_slab = target_slab->next;
    }
        
    // if we didnt find our target slab in the partial list, we check the full list
    if (!target_slab) {
        prev_slab = NULL;
        target_slab = cache->full_list;
        while (target_slab) {
            if (ptr >= target_slab->data 
                && ptr < (void*)((char*)target_slab->data + slab_bytes)) {
                located_in_full = true;
                break;
            }
            prev_slab = target_slab;
            target_slab = target_slab->next;
        }
    }

    if (!target_slab) return; // pointer does not belong to a slab cache

    // calculate which slot in the slab this was located in, clear it and and mark it as free
    u64 offset = (char*)ptr - (char*)target_slab->data;
    u32 global_index = offset / cache->object_size;
    u32 bitmask_number = global_index >> 6;
    u32 bit_number = global_index % 64;

    target_slab->bitmasks[bitmask_number] &= ~((u64)1 << bit_number);

    if (located_in_full) {
        // our slab was in the full list, but now since it has slots empty, we can move it to the partial list
        if (prev_slab) prev_slab->next = target_slab->next;
        else cache->full_list = target_slab->next;

        S_AddSlabToCachePartialList(cache, target_slab);
    }
    else {
        if (S_CheckSlabEmpty(cache, target_slab)) {
            // our slab was in the partial list, but now is completely empty, so move it to the free list
            if (prev_slab) prev_slab->next = target_slab->next;
            else cache->partial_list = target_slab->next;

            S_AddSlabToCacheFreeList(cache, target_slab);
        }
    }
}

void S_FreeSlabList(S_Slab* list_head) {
    S_Slab* cur = list_head;
    while (cur) {
        S_Slab* next = cur->next;
        free(cur->data);
        free(cur->bitmasks);
        free(cur);
        cur = next;
    }
}

/*
 * Free allocated memory and reset fields.
 */
void S_CacheDestroy(S_Cache* cache) {
    S_FreeSlabList(cache->partial_list);
    S_FreeSlabList(cache->free_list);
    S_FreeSlabList(cache->full_list);

    cache->partial_list = NULL;
    cache->free_list = NULL;
    cache->full_list = NULL;
}