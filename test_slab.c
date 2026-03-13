#include <stdio.h>

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#define CLOCK_MONOTONIC 1

#include "slab.h"

double S_GetPreciseTimeSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// otherwise hidden from the interface
struct S_Slab {
    void* data;
    unsigned long* bitmasks;
    unsigned int mask_count;
    S_Slab* next;
};

// TestObject is of 64 bytes
typedef struct {
    int id;
    char data[60];
} TestObject;

void S_TestInitialisation() {
    printf("Now testing: S_TestInitialisation() ... ");

    // initialisation
    S_Cache cache;
    S_CacheInit(&cache, sizeof(TestObject), 0);
    
    // check fields have initialised correctly
    assert(cache.object_size == 64);
    assert(cache.order == 0);
    assert(cache.partial_list == NULL);
    assert(cache.free_list == NULL);
    assert(cache.full_list == NULL);

    S_CacheDestroy(&cache);

    printf("Passed!\n");
}

void S_TestSingleAllocation() {
    printf("Now testing: S_TestSingleAllocation() ... ");
    S_Cache cache;
    S_CacheInit(&cache, sizeof(TestObject), 0);

    // allocate memory to new object
    TestObject* obj = (TestObject*)S_SlabAlloc(&cache);
    assert(obj != NULL); // check memory allocation success
    
    // check slab was added to partial list
    assert(cache.partial_list != NULL);
    assert(cache.partial_list->bitmasks[0]); // first bit marked
    
    obj->id = 42;
    assert(obj->id == 42);

    // after freeing the only object, it should move to the free list
    S_SlabFree(&cache, obj);
    assert(cache.partial_list == NULL);
    assert(cache.free_list != NULL);
    
    S_CacheDestroy(&cache);
    printf("Passed!\n");
}

void S_TestFullSlab() {
    printf("Now testing: S_TestFullSlab() ... ");
    S_Cache cache;
    S_CacheInit(&cache, 64, 0); // as struct size is 64, we should be able to fit 64 slots in each slab

    // allocate 64 times
    void* ptrs[64];
    for (int i = 0; i < 64; i++) {
        ptrs[i] = S_SlabAlloc(&cache);
        assert(ptrs[i] != NULL);
    }

    // slab should now be full, so it should be on full list
    assert(cache.full_list != NULL);
    assert(cache.partial_list == NULL);

    // once we free a slot from a full slab it should move to partial
    S_SlabFree(&cache, ptrs[32]);
    assert(cache.full_list == NULL);
    assert(cache.partial_list != NULL);

    S_CacheDestroy(&cache);
    printf("Passed!\n");
}

void S_TestMultipleSlabs() {
    printf("Now testing: S_TestMultipleSlabs() ... ");
    S_Cache cache;
    S_CacheInit(&cache, 2048, 0); // only 2 objects per slab, we don't really care what they are here we just want to see how multiple slabs work

    void* p1 = S_SlabAlloc(&cache);
    void* p2 = S_SlabAlloc(&cache); // slab 1 should now be full 
    assert(cache.full_list != NULL);

    void* p3 = S_SlabAlloc(&cache); // this should trigger the creation of another slab
    assert(cache.partial_list != NULL); 
    assert(cache.full_list != NULL);

    S_SlabFree(&cache, p1);
    S_SlabFree(&cache, p2); // slab 1 should now be free (empty) and put in the free list
    assert(cache.free_list != NULL);

    S_CacheDestroy(&cache);
    printf("Passed!\n");
}

void S_TestSlabsAreReused() {
    printf("Now testing: S_TestSlabsAreReused() ... ");
    S_Cache cache;
    S_CacheInit(&cache, 4096, 0); // one object per slab

    void* p1 = S_SlabAlloc(&cache); // allocate then immediately free, so the slab is free again
    S_SlabFree(&cache, p1); 
    assert(cache.free_list != NULL); // slab should now be in the free list

    void* p2 = S_SlabAlloc(&cache); // since there is a free slab, we should pull this instead of creating a new one
    assert(cache.free_list == NULL);
    assert(cache.partial_list != NULL || cache.full_list != NULL);
    assert(p1 == p2); // should have allocated to the same memory address

    S_CacheDestroy(&cache);
    printf("Passed!\n");
}

int main() {
    printf("Starting test_slab tests:\n");
    double start_time = S_GetPreciseTimeSeconds();
    S_TestInitialisation();
    S_TestSingleAllocation();
    S_TestFullSlab();
    S_TestMultipleSlabs();
    S_TestSlabsAreReused();
    double time_elapsed = S_GetPreciseTimeSeconds() - start_time;
    printf("All tests passed in %lfs.\n", time_elapsed);
    return 0;
}