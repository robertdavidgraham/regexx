#ifndef UTIL_BOOL_H
#define UTIL_BOOL_H

#if _MSC_VER && _MSC_VER < 1800
#define false 0
#define true 1
typedef int bool;
#else
#include <stdbool.h>
#endif
#endif

