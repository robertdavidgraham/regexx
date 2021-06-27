#include "c-preproc-tokens.h"
#include <stdlib.h>

void tokenlist_add(tokenlist_t *tokens, clextoken_t token) {
    
    tokens->list = realloc(tokens->list, (tokens->count + 1) * sizeof(token));
    if (tokens->list == NULL)
        abort();
    tokens->list[tokens->count++] = token;
}
