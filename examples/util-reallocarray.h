#ifndef REALLOCARRAY_H
#define REALLOCARRAY_H
#include <stdint.h>

void *reallocarray(void *p, size_t count, size_t elemsize);
void *duplicatearray(void *p, size_t count, size_t elemsize);

#endif

