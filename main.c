#include "slab.h"
#include <stdio.h>

// DEMO program

S_Cache cache;

typedef struct {
    float x, y;
} Vector2;

Vector2* newVec2(float x, float y) {
    Vector2* vec = (Vector2*)S_SlabAlloc(&cache);
    vec->x = x;
    vec->y = y;
    return vec;
}

void destroyVec2(Vector2* vec) {
    S_SlabFree(&cache, vec);
} 

void printVec2(Vector2* vec) {
    printf("(%f, %f)\n", vec->x, vec->y);
}

int main(void) {
    S_CacheInit(&cache, sizeof(Vector2), 0);

    Vector2* vec = newVec2(10.0f, 10.0f);
    vec->x = 5.0f;
    printVec2(vec);
    destroyVec2(vec);

    S_CacheDestroy(&cache);
    return 0;
}

