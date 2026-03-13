#include "slab.h"
#include <stdio.h>

typedef struct {
    float x, y;
} Vector2;

int main(void) {
    S_Cache cache;
    S_CacheInit(&cache, sizeof(Vector2), 0);

    // TODO: add test suite
    Vector2* vec = (Vector2*)S_SlabAlloc(&cache);
    vec->x = 1.0f;
    vec->y = 0.0f;
    printf("x => %f\n", vec->x);
    printf("y => %f\n", vec->y);
    S_SlabFree(&cache, vec);

    S_CacheDestroy(&cache);
    return 0;
}