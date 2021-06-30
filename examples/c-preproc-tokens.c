#include "c-preproc-tokens.h"
#include <stdlib.h>
#include <string.h>

void tokenlist_add(tokenlist_t *tokens, clextoken_t token) {
    
    tokens->list = realloc(tokens->list, (tokens->count + 1) * sizeof(token));
    if (tokens->list == NULL)
        abort();
    tokens->list[tokens->count++] = token;
}

static bool tokens_are_equal(clextoken_t lhs, clextoken_t rhs) {
    if (lhs.id != rhs.id)
        return false;
    switch (lhs.id) {
        case T_IDENTIFIER:
            if (lhs.string.length != rhs.string.length)
                return false;
            return memcmp(lhs.string.string, rhs.string.string, lhs.string.length) == 0;
        default:
            return false;
    }
}
bool tokenlist_has_identifier(tokenlist_t *tokens, clextoken_t token) {
    size_t i;

    for (i=0; i<tokens->count; i++) {
        if (tokens_are_equal(tokens->list[i], token))
            return true;
    }
    return false;
}
