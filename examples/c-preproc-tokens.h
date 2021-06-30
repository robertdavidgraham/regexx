#ifndef C_PREPROC_TOKENS_H
#define C_PREPROC_TOKENS_H
#include "c-lex.h"
#include <stdbool.h>

typedef struct tokenlist_t {
    clextoken_t *list;
    size_t count;
} tokenlist_t;

void tokenlist_add(tokenlist_t *tokens, clextoken_t token);
bool tokenlist_has_identifier(tokenlist_t *tokens, clextoken_t token);

void tokenlist_free(tokenlist_t *tokens);


#endif

