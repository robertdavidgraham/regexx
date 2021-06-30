#include "c-preproc-macros.h"
#include "c-errors.h"
#include "c-preproc-tokens.h"
#include "util-hashmap.h"
#include "util-siphash24.h"
#include "util-reallocarray.h"
#include <stdbool.h>
#include <string.h>

/**
 * Some global entropy for hashing, so that attackers can't attack the compiler.
 * TODO: set this to a different value at program startup
 */
static uint64_t global_entropy[2] = {1,2};


typedef struct macro_t {
    bool is_function;
    clextoken_t name;
    tokenlist_t parms;
    tokenlist_t body;
} macro_t;

struct ppmacros_t {
    hashmap_t *macros;
};


/**
 * Hash the name of a macro, in typical hash-table fashion.
 */
static uint64_t _macroname_hash(const void* key) {
    const char *name = (const char *)key;
    size_t name_length = strlen(name);
    return siphash24(name, name_length, global_entropy);
}

/**
 * Callback function to test whether two hashmap keys are the same.
 * Our hashmap "key" will be the macro name, so it's a simple string compare.
 */
static bool _macroname_is_equal(const void* keyA, const void* keyB) {
    clextokenstring_t *strA = (clextokenstring_t*)keyA;
    clextokenstring_t *strB = (clextokenstring_t*)keyB;
    if (strA->length != strB->length)
        return false;
    
    return memcmp(strA->string, strB->string, strA->length) == 0;
}

ppmacros_t *ppmacros_create(void) {
    ppmacros_t *ppm;
    
    ppm = calloc(1, sizeof(*ppm));
    if (ppm == NULL)
        abort();
    
    ppm->macros = hashmap_create(256, _macroname_hash, _macroname_is_equal);
    if (ppm->macros == NULL)
        abort();
    
    return ppm;
}

static bool macros_are_equal(macro_t *macro, clextoken_t name, bool is_function, tokenlist_t parms, tokenlist_t body) {
    size_t i;
    if (!clex_tokens_are_equal(macro->name, name))
        return false;
    if (macro->is_function != is_function)
        return false;
    if (macro->parms.count != parms.count)
        return false;
    for (i=0; i<parms.count; i++) {
        if (!clex_tokens_are_equal(macro->parms.list[i], parms.list[i]))
            return false;
    }
    return true;
}

static tokenlist_t _normalize_whitespace(tokenlist_t body) {
    size_t i;

    /* Remove leading and trailing whitespace/comments */
    while (body.count && (body.list[0].id == T_WHITESPACE || body.list[0].id == T_WHITESPACE)) {
        body.count--;
        memmove(body.list, body.list+1, body.count * sizeof(body.list[0]));
    }
    while (body.count && (body.list[body.count-1].id == T_WHITESPACE || body.list[body.count-1].id == T_WHITESPACE)) {
        body.count--;
    }

    /* Go through all tokens looking for whitespade and comments */
    for (i=0; i<body.count; i++) {
        clextoken_t *tok = &body.list[i];
        clextoken_t *prev = &body.list[i-1];

        /* Skip things that aren't whitespace/comments */
        if (tok->id != T_COMMENT && tok->id != T_WHITESPACE)
            continue;

        /* Only one whitespace at a time, so if followed by another
         * whitespace/comment, simply delete the second one */
        if (prev->id == T_WHITESPACE) {
            body.count--;
            memmove(tok, tok+1, body.count * sizeof(*tok));
            continue;
        }

        /* Change this to a single whitespace character (even if it was
         * a comment) */
        tok->id = T_WHITESPACE;
        tok->string.string = " ";
        tok->string.length = 1;
    }

    return body;
}


int ppmacros_add(ppmacros_t *ppm, const clextoken_t name, bool is_function,
                 const tokenlist_t parms, const tokenlist_t in_body)
{
    tokenlist_t body;
    macro_t *macro;

    /*
     * Normalize the whitespace. This reduces all comments or whitespace
     * into a single whitespace " " character, removing back-to-back
     * duplicates. After this transformation, if there was a comment
     * or whitespace between tokens, there is now a single whitespace
     * character.
     */
    body = _normalize_whitespace(in_body);

    /*
     * Test if the macro already exists. It is no error to have have the
     * same definition, in which case we return immediately with a success
     * code. When the definition is different, then we return an error.
     */
    macro = hashmap_get(ppm->macros, &name.string);
    if (macro) {
        if (macros_are_equal(macro, name, is_function, parms, body))
            return 0; /* success */
        else {
            return -1; /* error */
        }
    }

    /*
     * Create a new macro entry. We own
     */
    macro = calloc(1, sizeof(*macro));
    if (macro == NULL)
        return C_ERR_OUT_OF_MEMORY;
    macro->is_function = is_function;
    macro->name = name;
    macro->parms.list = duplicatearray(parms.list, parms.count, sizeof(parms.list[0]));
    macro->parms.count = parms.count;
    macro->body.list = duplicatearray(body.list, body.count, sizeof(body.list[0]));
    macro->body.count = body.count;
    
    hashmap_put(ppm->macros, &macro->name.string, macro);
    
    return 0;
}

const ppmacro_t *ppmacros_lookup(const ppmacros_t *ppm, const clextoken_t name)
{
    return hashmap_get(ppm->macros, &name.string);
}
