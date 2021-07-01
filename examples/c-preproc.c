#include "c-preproc.h"
#include "c-errors.h"
#include "c-preproc-tokens.h"
#include "c-preproc-macros.h"
#include "c-lex.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef WIN32
#define fileno _fileno
#define strdup _strdup
#endif

#if defined(_MSC_VER)
typedef int ssize_t;
#endif

static bool is_debug = true;
static bool is_debug_recursion = true;

#define _ENTER(name) if (is_debug_recursion) printf("-->%s %u\n", name, depth)
#define _EXIT(name) if (is_debug_recursion) printf("<--%s %u\n", name, depth)

enum preprocexptype {
    PPT_INCLUDE,
    PPT_DEFINE_CONSTANT,
    PPT_DEFINE_FUNC,
    PPT_LINE,
};



typedef struct ppfile_t {
    char *filename;
    char *buf;
    size_t length;
    size_t offset;
    unsigned include_depth;
} ppfile_t;


/*
 * This is our master object for a translation unit
 */
typedef struct translationunit_t {
    clex_t *clex;
    tokenlist_t tokens;
    ppmacros_t *macros;
    ppfile_t *files;
    size_t file_count;
} translationunit_t;

static int preproc_phase3_tokenize(translationunit_t *pp, ppfile_t *file, size_t depth, bool is_if, bool is_else);


static void _ppfile_free(ppfile_t *file) {
    free(file->filename);
    free(file->buf);
}


void preproc_free(translationunit_t *pp) {
    size_t i;
    clex_free(pp->clex);
    for (i=0; i<pp->file_count; i++) {
        _ppfile_free(&pp->files[i]);
    }
    free(pp->files);
    free(pp);
}


/**
 * Create an object to track the file, either the main
 * translation unit file, or one of the nested #include
 * files.
 */
static ppfile_t _ppfile_create(const  char *filename) {
    ppfile_t file = {0};
    FILE *fp;
    char *buf;
    size_t length;
    ssize_t bytes_read;

    /*
     * Open the file
     * TODO: allow "-" as a filename representing <stdin>
     */
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "[-] %s: %s\n", filename, strerror(errno));
        return file;
    }

    /*
     * Discover the size of the file.
     * TODO: we need to allow streaming, in which case, the filesize
     * TODO:  will be unknown at the start.
     */
    {
        struct stat st = {0};
        if (fstat(fileno(fp), &st) != 0 || st.st_size == 0) {
            fprintf(stderr, "[-] %s: %s\n", filename, strerror(errno));
            return file;
        }
        length = st.st_size;
    }

    /*
     * Read the entire file into memory
     * TODO: we should iterate over chunks of the file instead
     * TODO: of holding the entire thing in memory
     */
    buf = malloc(length+1+1);
    if (buf == NULL)
        abort();
    bytes_read = fread(buf, 1, length, fp);
    if (bytes_read < 0 || bytes_read < (signed)length) {
        free(buf);
        return file;
    }
    if (length && buf[length-1] != '\n') {
        /* make sure file always ends in a newline */
        buf[length] = '\n';
        length++;
    }
    buf[length] = '\0'; /* nul terminate for debugging */
    fclose(fp);

    /*
     * Now that we have a legitimately open file, return the
     * result.
     */
    file.filename = strdup(filename);
    file.buf = buf;
    file.length = length;
    file.offset = 0;

    return file;
}

translationunit_t *preproc_create(const char *filename, struct clex_t *clex) {
    translationunit_t *pp;
    ppfile_t file;


    /*
     * If we don't have a clex context, create one
     */
    if (clex == NULL)
        clex = clex_create();


    /*
     * Open the file
     */
    file = _ppfile_create(filename);
    if (file.filename == NULL || file.buf == NULL) {
        fprintf(stderr, "[-] %s: couldn't open file\n", filename);
        return NULL;
    }

    /*
     * Now create the preprocessor object
     */
    pp = malloc(sizeof(*pp));
    if (pp == NULL)
        abort();
    memset(pp, 0, sizeof(*pp));
    pp->clex = clex;

    /*
     * Append the file
     */
    pp->files = malloc(sizeof(file));
    pp->files[0] = file;
    pp->file_count = 1;

    clex_push(clex); /* save previous context */

    /*
     * Save the table of macros
     */
    pp->macros = ppmacros_create();
    return pp;
};


static void _debug(const clextoken_t token) {
        switch (token.id) {
        case T_COMMENT: 
            printf("{/**/}");
            break;
        case T_WHITESPACE: printf("{ }"); break;
        case T_NEWLINE: 
            printf("{\\n %u}\n", (unsigned)token.line_number); 
            break;
        default:
            printf("{%.*s}", (unsigned)token.string.length, token.string.string);
        }
}
static clextoken_t _next(translationunit_t *pp, ppfile_t *file) {
    if (is_debug) {
        clextoken_t token = clex_next(pp->clex, file->buf, &file->offset, file->length);
        _debug(token);
        return token;
    } else
        return clex_next(pp->clex, file->buf, &file->offset, file->length);
}

/**
 * This 'trims' the input, skipping all whitespace and comments
 * until the next token. It does add these to the tokenized
 * list, though, because subsequent phases might want to see them.
 */
clextoken_t _trimadd(translationunit_t *pp, ppfile_t *file) {
    clextoken_t token = _next(pp, file);
    while (token.id == T_WHITESPACE || token.id == T_COMMENT) {
        tokenlist_add(&pp->tokens, token);
        token = _next(pp, file);
    } 
    return token;
}

/**
 * Same as `_trim()` (trims whitespace/comments), but doesn't
 * add them to to our token list (it skips them without
 * recording them) */
clextoken_t _trimskip(translationunit_t *pp, ppfile_t *file) {
    clextoken_t token;
    do {
        token = _next(pp, file);
        //tokenlist_add(&pp->tokens, token);
    } while (token.id == T_WHITESPACE || token.id == T_COMMENT);
    return token;
}


static int ERROR(const ppfile_t *file, const clextoken_t token, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    fprintf(stderr, "[-] %s:%u:%u: ",
            file->filename,
            (unsigned)token.line_number,
            (unsigned)token.char_number);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    return -1;
}
static int ERR_UNEXPECTED(ppfile_t *file, clextoken_t token, int id_expected) {
    fprintf(stderr, "[-] %s:%u:%u: unexpected '%s', was expecting '%s'\n",
            file->filename,
            (unsigned)token.line_number,
            (unsigned)token.char_number,
            clex_token_name(token),
            clex_tokenid_name(id_expected));
    return -1;
}

static int _get_pp_directive(clextoken_t token) {
    static const struct {
        int id;
        const char *directive;
        size_t length;
    } directives[] = {
        {T__DEFINE, "define", 6},
        {T__INCLUDE, "include", 7},
        {T__IFDEF, "ifdef", 5},
        {T__IFNDEF, "ifndef", 6},
        {T__IF, "if", 2},
        {T__ELIF, "elif", 4},
        {T__ELSE, "else", 4},
        {T__ENDIF, "endif", 5},
        {T__LINE, "line", 4},
        {T__UNDEF, "undef", 5},
        {T__ERROR, "error", 5},
        {T__WARNING, "warning", 7},
        {T__PRAGMA, "pragma", 6},
        {0,0}
    };
    size_t i;

    for (i=0; directives[i].directive; i++) {
        if (token.string.length != directives[i].length)
            continue;
        if (memcmp(token.string.string, directives[i].directive, token.string.length) == 0)
            return directives[i].id;
    }

    return -1;
}



/**
 * Handle what happens when we need to skip content due to a failed
 * `#if`/`#ifdef`/`#elif` condition.
 */
static int _process_SKIP(translationunit_t *pp, ppfile_t *file, size_t depth, bool is_inside_else, bool is_everything) {
    int err;
    bool has_seen_else = is_inside_else;

    /*
     * Process one line at a time
     */
    while (file->offset < file->length) {
        clextoken_t token;
        clextoken_t directive;

        /* Strip whitespace from start of line */
        token = _trimskip(pp, file);

        /* If the first non-space token is not the # preprocessor
         * directive, then simply skip tokens until end-of-line */
        if (token.id == T_NEWLINE)
            continue;
        if (token.id != T__POUND) {
            do {
                token = _next(pp, file);
                //tokenlist_add(&pp->tokens, token);
            } while (token.id != T_NEWLINE);

            /* process next line */
            continue;
        }


        /* Trim any comments and space betwee `#` and `define`.
         * These aren't recorded in the tokenization */
        token = _trimskip(pp, file);

        /* This could be a naked `#` on a line */
        if (token.id == T_NEWLINE) {
            continue;
        }

        /* Clone the directive token and change it's 'id' */
        directive = token;
        directive.id = _get_pp_directive(token);
        if (directive.id == -1) {
            ERROR(file, token, "invalid preprocessing directive `#%.*s`",
                  (unsigned)token.string.length, token.string.string);
            return -1;
        }

        /* Trim whitespace/commetns after the directive, ie.
         * the space between the two: `#define X` */
        token = _trimskip(pp, file);

        /* Regardless of outcome, skip to the end-of-line */
        while (token.id != T_NEWLINE)
            token = _next(pp, file);

        /* Now do individual processing for the directive */
        switch (directive.id) {
            case T__DEFINE:
            case T__INCLUDE:
            case T__WARNING:
            case T__ERROR:
            case T__LINE:
            case T__PRAGMA:
                break;
            case T__IF:
            case T__IFDEF:
            case T__IFNDEF:
                /* We need to skip these recursively, as if this expression
                 * failed. First we need to process till the end-of-line */
                _ENTER("skip");
                err = _process_SKIP(pp, file, depth+1, false, true);
                _EXIT("skip");
                if (err) {
                    ERROR(file, directive, "failed define");
                    goto fail;
                }
                break;
            case T__ELSE:
                if (has_seen_else) {
                    /* If we already have seen an `#else` statement, this is
                     * an error */
                    ERROR(file, directive, "#else after #else");
                    goto fail;
                }
                if (!is_everything) {
                    return C_NORM_ELSE;
                } else {
                    /* We are recursively inside a skipped section, so
                     * both sides of if/else are skipped */
                    has_seen_else = true;
                }
                break;
            case T__ENDIF:
                /* we are done */
                return 0;
            default:
                /* we allow invalid preprocessing directives here */
                //ERROR(file, directive, "invalid preprocessing directive");
                return -1;
        }

    }

    return 0;
fail:
    return -1;
}

/**
 * Handle the `#ifdef` direct. This includes evaluating the defined
 * macro, as well as skipping to the `#else` or `#elif` statements.
 */
static int _process_WARNING(translationunit_t *pp, ppfile_t *file, const clextoken_t warntok) {
    clextoken_t token;
    char *warning = NULL;
    size_t warning_length = 0;


    warning = malloc(1);
    warning[0] = '\0';

    /* Print everything until end-of-line, including whitespace/comments */
    for (;;) {
        token = _next(pp, file);
        if (token.id == T_NEWLINE)
            break;
        warning = realloc(warning, warning_length + token.string.length + 1);
        memcpy(warning + warning_length, token.string.string, token.string.length);
        warning[warning_length + token.string.length] = '\0';
        warning_length += token.string.length;
    }

    ERROR(file, warntok, "%.*s", (unsigned)warning_length, warning);

    return 0;
}

/**
 * Handle the `#ifdef` direct. This includes evaluating the defined
 * macro, as well as skipping to the `#else` or `#elif` statements.
 */
static int _process_IFDEF(translationunit_t *pp, ppfile_t *file, size_t depth, bool is_inverted) {
    clextoken_t token;
    const ppmacro_t *macro;
    bool is_found;
    int err;


    /*
     * Get the first non-whitespace token, which should be the identifier.
     */
    token = _trimskip(pp, file);
    if (token.id != T_IDENTIFIER && token.id != T_KEYWORD) {
        ERROR(file, token, "macro name missing");
        return -1;
    }

    /*
     * lookup the macro
     */
    macro = ppmacros_lookup(pp->macros, token);
    is_found = (macro != NULL);
    if (is_inverted)
        is_found = !is_found;


    /*
     * Now continue processing from here
     */
    if (is_found) {
        _ENTER("parse");
        err = preproc_phase3_tokenize(pp, file, depth+1, true, false);
        _EXIT("parse");
        if (err == C_NORM_ELSE) {
            _ENTER("skip");
            err = _process_SKIP(pp, file, depth+1, true, false);
            _EXIT("skip");
        } else if (err)
            return err;
    } else {
        _ENTER("skip");
        err = _process_SKIP(pp, file, depth+1, false, false);
        _EXIT("skip");
        if (err == C_NORM_ELSE) {
            _ENTER("parse");
            err = preproc_phase3_tokenize(pp, file, depth+1, false, true);
            _EXIT("parse");
        } else if (err)
            return err;
    }

    return err;
}

static int _process_ARGS(translationunit_t *pp, ppfile_t *file, tokenlist_t *parms) {
    clextoken_t token;
    
    /* Get rid of any whitespace in the argument list */
    token = _trimskip(pp, file);

    for (;;) {
        /* if the ellipses (for variable arguments) exists
         * in the list, then it must be at the end.
         * Therefore, check for ending ')' immediately after */
        if (token.id == T_ELLIPSES) {
            tokenlist_add(parms, token);
            token = _trimskip(pp, file);
            if (token.id != T_PARENS_CLOSE)
                return ERR_UNEXPECTED(file, token, T_PARENS_CLOSE);
            break;
        }

        /* Add the token to our argument list  */
        if (token.id == T_IDENTIFIER || token.id == T_KEYWORD) {
            if (tokenlist_has_identifier(parms, token))
                return ERROR(file, token, "duplicate macro arg");
            tokenlist_add(parms, token);
            token = _trimskip(pp, file);
        } else if (token.id == T_COMMA || token.id == T_PARENS_CLOSE) {
            clextoken_t clone;

            /* We have an empty argument, in which case,
             * we need to add that. We need to clone this
             * token (for filename/line-number/char-number)
             * but make it an empty token */
            clone = token;
            clone.id = T_WHITESPACE;
            clone.string.length = 0;
            tokenlist_add(parms, clone);
        }

        /* We've either come to the end of our list or
         * see a comma `,` meaning we need to loop around again */
        if (token.id == T_NEWLINE) {
            return ERR_UNEXPECTED(file, token, T_PARENS_CLOSE);
        }
        if (token.id == T_PARENS_CLOSE) {
            break;
        }
        if (token.id != T_COMMA) {
            return ERR_UNEXPECTED(file, token, T_PARENS_CLOSE);
        }

        token = _trimskip(pp, file);
    }

    return 0;
}

static int _process_PARMS(translationunit_t *pp, ppfile_t *file, tokenlist_t *parms) {
    clextoken_t token;
    
    /* Get rid of any whitespace in the argument list */
    token = _trimskip(pp, file);

    for (;;) {
        /* if the ellipses (for variable arguments) exists
         * in the list, then it must be at the end.
         * Therefore, check for ending ')' immediately after */
        if (token.id == T_ELLIPSES) {
            tokenlist_add(parms, token);
            token = _trimskip(pp, file);
            if (token.id != T_PARENS_CLOSE)
                return ERR_UNEXPECTED(file, token, T_PARENS_CLOSE);
            break;
        }

        /* Add the token to our argument list  */
        if (token.id == T_IDENTIFIER || token.id == T_KEYWORD) {
            if (tokenlist_has_identifier(parms, token))
                return ERROR(file, token, "duplicate macro arg");
            tokenlist_add(parms, token);
            token = _trimskip(pp, file);
        } else if (token.id == T_COMMA || token.id == T_PARENS_CLOSE) {
            clextoken_t clone;

            /* We have an empty argument, in which case,
             * we need to add that. We need to clone this
             * token (for filename/line-number/char-number)
             * but make it an empty token */
            clone = token;
            clone.id = T_WHITESPACE;
            clone.string.length = 0;
            tokenlist_add(parms, clone);
        }

        /* We've either come to the end of our list or
         * see a comma `,` meaning we need to loop around again */
        if (token.id == T_NEWLINE) {
            return ERR_UNEXPECTED(file, token, T_PARENS_CLOSE);
        }
        if (token.id == T_PARENS_CLOSE) {
            break;
        }
        if (token.id != T_COMMA) {
            return ERR_UNEXPECTED(file, token, T_PARENS_CLOSE);
        }

        token = _trimskip(pp, file);
    }

    return 0;
}

/**
 * Handle the `#define` directive. There are two types, the simple and
 * the function-like macro. We need to add this symbol to our table, and then
 * whenever we see the identifier in the stream, replace the contents with
 * the macro.
 */
static int _process_DEFINE(translationunit_t *pp, ppfile_t *file) {
    int err = 0;
    clextoken_t identifier;
    tokenlist_t parms = {0,0};
    tokenlist_t body = {0,0};
    bool is_function = false;
    clextoken_t token;
    

    /*
     * Remove any trailing whitespace or comments after the
     * "#define" keyword
     */
    token = _trimskip(pp, file);
    if (token.id != T_IDENTIFIER && token.id != T_KEYWORD) {
        ERROR(file, token, "macro name missing");
        return -1;
    }

    /*
     * Grab the identifier. This will be the 'name' of the macro
     */
    identifier = token;

    /*
     * If the immediate (no whitespace) next character is a parentheses,
     * then we have a function-like macro. At this stage, we need to
     * parse the args.
     * We can have multiple forms:
     *   #define FOO()     x x x
     *   #define FOO(...)  x x x
     *   #define FOO(a)    x x x
     *   #define FOO(a,b)  x x x
     *   #define FOO(,b)   x x x
     *   #define FOO(a,)   x x x
     *   #define FOO(,)    x x x
     */
    token = _next(pp, file);
    if (token.id == T_PARENS_OPEN) {
        is_function = true;

        err = _process_PARMS(pp, file, &parms);
        if (err)
            goto fail;

        token = _next(pp, file);
    }

    if (token.id == T_WHITESPACE || token.id == T_COMMENT)
        token = _trimskip(pp, file);

    /*
     * Now add the replacement-list of the token
     */
    while (token.id != T_NEWLINE) {
        tokenlist_add(&body, token);
        token = _next(pp, file);
    }

    /*
     * Now insert this macro into our list. If it already exists,
     * it will still succeed if the definition is the same, otherwise,
     * it will fail.
     */
    err = ppmacros_add(pp->macros, identifier, is_function, parms, body);
    if (err) {
        ERROR(file, identifier, "duplicate macro definition");
        goto fail;
    }

    /* We made a copy of these when we inserted into the hash table */
    free(parms.list);
    free(body.list);
    return 0;
fail:
    free(parms.list);
    free(body.list);
    return err;
}

/**
 * Add 'preprocessor-tokens' as normal tokens. This may require macro
 * replacement.
 */
static int preproc_add_token(translationunit_t *pp, ppfile_t *file, clextoken_t token) {
    if (token.id == T_IDENTIFIER || token.id == T_KEYWORD) {
        const ppmacro_t *macro = ppmacros_lookup(pp->macros, token);
        if (macro && macro->is_function) {
            tokenlist_t args = {0,0};
            int err;
            size_t arg_count;
            err = _process_PARMS(pp, file, &args);
            if (err) {
                ERROR(file, token, "failed to read macro arguments");
                return err;
            }
            arg_count = args.count;
            if (arg_count && args.list[arg_count-1].id == T_ELLIPSES) {

            }
            return 0;
        } else if (macro) {
            return 0;
        }
    }

    tokenlist_add(&pp->tokens, token);
    return 0;
}

/**
 * This is where files are read in, tokenized, and pre-processed.
 * The will get called recursively to process #include files.
 * @param is_if
 *  Are we inside an `#if` block? If so, we need to terminate once
 *  we get an `#endif`.
 */
static int preproc_phase3_tokenize(translationunit_t *pp, ppfile_t *file, size_t depth, bool is_if, bool is_else) {
    int err;

    /*
     * Loop through all tokens, fundamentally processing
     * a line at a time.
     */
    while (file->offset < file->length) {
        clextoken_t token;
        clextoken_t directive;

        /* Strip whitespace from start of line */
        token = _trimadd(pp, file);
        if (token.id == T_NEWLINE) {
            preproc_add_token(pp, file, token);
            continue;
        }

        /* If the first non-space token is not the # preprocessor
         * directive, then process skip tokens until end-of-line */
        if (token.id != T__POUND) {
            preproc_add_token(pp, file, token);
            do {
                token = _next(pp, file);
                preproc_add_token(pp, file, token);
            } while (token.id != T_NEWLINE);
            continue;
        }


        /* Trim any comments and space betwee `#` and `define`.
         * These aren't recorded in the tokenization */
        token = _trimskip(pp, file);

        /* This could be a naked `#` on a line */
        if (token.id == T_NEWLINE) {
            //tokenlist_add(&pp->tokens, token);
            continue;
        }

        /* Clone the directive token and change it's 'id' */
        directive = token;
        directive.id = _get_pp_directive(token);
        if (directive.id == -1) {
            ERROR(file, token, "invalid preprocessing directive `#%.*s`",
                   (unsigned)token.string.length, token.string.string);
            return -1;
        }

        /* Now do individual processing for the directive */
        switch (directive.id) {
            case T__DEFINE:
                err = _process_DEFINE(pp, file);
                if (err) {
                    ERROR(file, token, "failed define");
                    goto fail;
                }
                break;
            case T__IFDEF:
                _ENTER("ifdef");
                err = _process_IFDEF(pp, file, depth+1, false);
                _EXIT("ifdef");
                if (err) {
                    ERROR(file, token, "failed #if");
                    goto fail;
                }
                break;
            case T__IFNDEF:
                _ENTER("ifndef");
                err = _process_IFDEF(pp, file, depth+1, true);
                _EXIT("ifndef");
                if (err) {
                    ERROR(file, token, "failed #if");
                    goto fail;
                }
                break;
            case T__ELSE:
                if (is_if && !is_else) {
                    return C_NORM_ELSE;
                } else if (is_else) {
                    return ERROR(file, directive, "#else in #else");
                } else {
                    return ERROR(file, directive, "#else without #if");
                }
                break;
            case T__ENDIF:
                if (is_if || is_else) {
                    token = _trimskip(pp, file);
                    if (token.id != T_NEWLINE)
                        return ERROR(file, token, "extra tokens after preprocessor directive");
                    return 0;
                } else {
                    return ERROR(file, directive, "#endif without #if");
                }
                break;
            case T__WARNING:
                err = _process_WARNING(pp, file, directive);
                if (err) {
                    ERROR(file, token, "failed #warning");
                    goto fail;
                }
                break;

            case T__ERROR:
                err = _process_WARNING(pp, file, directive);
                if (err) {
                    ERROR(file, token, "failed #warning");
                    goto fail;
                }
                return -1; /* error means error */

            default:
                ERROR(file, directive, "unknown processor directive \'.*s\'",
                      (unsigned)directive.string.length,
                      directive.string.string);
                return -1;
        }

    }

    return 0;
fail:
    return -1;
}



int preproc_phase4_preprocess(translationunit_t *pp) {
    return 0;
}

static int preproc_phase1_charset(translationunit_t *pp) {
    /* Do nothing.
     *
     * CHARSET:
     * Right now, we are assuming everything is UTF-8. In
     * the future, we might add conversion for EBCDIC, or
     * allow generic converstion with the `iconv` library.
     *
     * TRIGRAPHS:
     * These are deprecated, so we are going to assume
     * they don't exist. I'll fix it later if this
     * is a problem.
     *
     * CRLF:
     * The standard is unclear how Microsoft's CRLF line
     * endings are handled. Right now, I do it as part
     * of the lexical analysis, where an end-of-line is
     * matches the regexp [\r]*[\n].
     *
     * This is something I really don't want to do because
     * it'll mess up character counts on lines, though
     * I'm not sure how that is handled anyway with UTF-8
     * sequences.
     */
    return 0;
}

static int preproc_phase2_linesplice(translationunit_t *pp) {
    /* Do nothing.
     *
     * LINE-SPLICE
     * Lines ending in \ are spliced together in this phase.
     * I'm currently solving this by just using complicated
     * regexp when doing lexical analysis in the next phase.
     *
     * If I were to do this as a separate step, I'd have
     * to do some magic to track line-numbers and character-numbers.
     * I'm not sure how to do that.
     */
    return 0;
}



int preproc_parse(translationunit_t *pp) {
    int err;
    
    err = preproc_phase1_charset(pp);
    if (err)
        return err;
    
    err = preproc_phase2_linesplice(pp);
    if (err)
        return err;
    
    err = preproc_phase3_tokenize(pp, &pp->files[0], 0, false, false);
    if (err)
        return err;
    
    err = preproc_phase4_preprocess(pp);
    if (err)
        return err;
    
    return 0; /* success */
}
