#include "util-reallocarray.h"
#include <stdlib.h>
#include <string.h>

#define MAXNUM ((size_t)1 << (sizeof(size_t)*4))


void *
reallocarray(void *p, size_t count, size_t size)
{
    if (count >= MAXNUM || size >= MAXNUM) {
        if (size != 0 && count >= SIZE_MAX/size) {
            //fprintf(stderr, "[-] alloc too large, aborting\n");
            abort();
        }
    }

    p = realloc(p, count * size);
    if (p == NULL && count * size != 0) {
        //fprintf(stderr, "[-] out of memory, aborting\n");
        abort();
    }

    return p;
}

void *
duplicatearray(void *p, size_t count, size_t size)
{
    void *p2 = reallocarray(0, count, size);
    memcpy(p2, p, count * size);
    return p2;
}



