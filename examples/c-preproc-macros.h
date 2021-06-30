#ifndef C_PREPROC_MACROS_H
#define C_PREPROC_MACROS_H
#include "c-lex.h"
#include "c-preproc-tokens.h"
#include <stdbool.h>

typedef struct ppmacros_t ppmacros_t;
typedef struct ppmacro_t {
    tokenlist_t macro_name;
    tokenlist_t args;
    tokenlist_t replacement;
} ppmacro_t;

ppmacros_t *ppmacros_create(void);
void ppmacros_free(ppmacros_t *pp);

/**
 * Given a #define macro definition, add it to our hash-table for this
 * translation-unit. If this is a non-matching duplicate, then an error
 * will be returned. This will be called every time we see a `#define`
 * preprocessor statement.
 * @param pp
 *  This translation-unit being parsed.
 * @param name
 *  The name of macro, that when found after this point, will be replaced
 *  by the tokens in this macro.
 * @param is_function
 *  Whether this is a function-like definition (true) or a normal macro (false)
 * @param args
 *  A list of the arguments. It may be empty. The last argument may be "...".
 *  If not a function-like macro (is_function==false), then this will be
 *  empty.
 * @param body
 *  The replacement-list for the macro.
 */
int ppmacros_add(ppmacros_t *pp, clextoken_t name, bool is_function, tokenlist_t args, tokenlist_t body);

/**
 * This will be valled for every "identifier" token we see in the input, to
 * test if a macro exists, and if it does, then do the replacement.
 */
const ppmacro_t *ppmacros_lookup(const ppmacros_t *pp, const clextoken_t name);



#endif

