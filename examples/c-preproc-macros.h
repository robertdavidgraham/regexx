#ifndef C_PREPROC_MACROS_H
#define C_PREPROC_MACROS_H
#include "c-lex.h"
#include "c-preproc-tokens.h"
#include <stdbool.h>

typedef struct ppmacros_t ppmacros_t;

ppmacros_t *ppmacros_create(void);
void ppmacros_free(ppmacros_t *pp);

int ppmacros_add(ppmacros_t *pp, clextoken_t name, bool is_function, tokenlist_t args, tokenlist_t body);



#endif

