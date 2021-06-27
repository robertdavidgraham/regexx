#ifndef C_PREPROC_TOKENS_H
#define C_PREPROC_TOKENS_H
#include "c-lex.h"

typedef struct tokenlist_t {
    clextoken_t *list;
    size_t count;
} tokenlist_t;

void tokenlist_add(tokenlist_t *tokens, clextoken_t token);

void tokenlist_free(tokenlist_t *tokens);


#endif

