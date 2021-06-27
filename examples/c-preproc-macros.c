#include "c-preproc-macros.h"
#include "c-errors.h"
#include "c-preproc-tokens.h"
#include "util-hashmap.h"
#include "util-siphash24.h"
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
    tokenlist_t args;
    tokenlist_t body;
} macro_t;

struct ppmacros_t {
    hashmap_t *macros;
};


/**
 * Hash the name of a macro, in typical hash-table fashion.
 */
static uint64_t _macroname_hash(void* key) {
    const char *name = (const char *)key;
    size_t name_length = strlen(name);
    return siphash24(name, name_length, global_entropy);
}

/**
 * Callback function to test whether two hashmap keys are the same.
 * Our hashmap "key" will be the macro name, so it's a simple string compare.
 */
static bool _macroname_is_equal(void* keyA, void* keyB) {
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


int ppmacros_add(ppmacros_t *ppm, clextoken_t name, bool is_function, tokenlist_t args, tokenlist_t body)
{
    macro_t *macro;
    
    /* First, lookup the name to see if it already exists */
    if (hashmap_is_contained_in(ppm->macros, &name.string)) {
        return C_ERR_PREPROC_REDEFINITION;
    }
    
    macro = calloc(1, sizeof(*macro));
    if (macro == NULL)
        return C_ERR_OUT_OF_MEMORY;
    macro->name = name;
    macro->args = args;
    macro->body = body;
    
    hashmap_put(ppm->macros, &name.string, macro);
    
    return 0;
}
