// testing \
more testing \
more testing
 \
  \
  //
 //
//

// test comment
/* test'test */
#define FOOB *
#ifdef FOO /* test
test*/
#warning hello
/* 
 */ #endif /* test
test*/
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
#include <stdbool.h>


/* For working token lists */
#define TOK(pp,index) pp->tokens.list[index]
#define TRIM(pp,index) while (TOK(pp,index).id == T_WHITESPACE || TOK(pp,index).id == T_COMMENT) {(index)++;}
#define SKIP(pp,index,type) if (TOK(pp,index).id != type) goto failed_unexpected; else index++
#define SKIP2(pp,index,type1,type2) if (TOK(pp,index).id != type1 && TOK(pp,index).id != type2) goto failed_unexpected; else index++


enum preprocexptype {
    PPT_INCLUDE,
    PPT_DEFINE_CONSTANT,
    PPT_DEFINE_FUNC,
    PPT_LINE,
};



/*
 * This is our master object. We'll create one per file, thus
 * creating a new one for every include statement. They can
 * all share the same `clex` object, however.
 */
typedef struct preprocessor_t {
    clex_t *clex;
    const char *filename;
    char *buf;
    size_t length;
    size_t offset;
    tokenlist_t tokens;
    ppmacros_t *macros;
} preprocessor_t;




static clextoken_t preproc_next(preprocessor_t *pp) {
    return clex_next(pp->clex, pp->buf, &pp->offset, pp->length);
}

void preproc_free(preprocessor_t *pp) {
    clex_free(pp->clex);
    free(pp->buf);
    free(pp);
}



preprocessor_t *preproc_create(const char *filename, struct clex_t *clex) {
    preprocessor_t *pp;
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
        return NULL;
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
            return NULL;
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
    if (bytes_read < 0 || bytes_read < length) {
        free(buf);
        return NULL;
    }
    if (length && buf[length-1] != '\n') {
        /* make sure file always ends in a newline */
        buf[length] = '\n';
        length++;
    }
    buf[length] = '\0'; /* nul terminate for debugging */
    fclose(fp);

    /*
     * If we don't have a clex context, create one
     */
    if (clex == NULL)
        clex = clex_create();
    
    /*
     * Now create the preprocessor object
     */
    pp = malloc(sizeof(*pp));
    if (pp == NULL)
        abort();
    memset(pp, 0, sizeof(*pp));
    pp->clex = clex;
    pp->filename = filename;
    pp->buf = buf;
    pp->length = length;
    clex_push(clex); /* save previous context */
    pp->macros = ppmacros_create();
    
    return pp;
};




static int _process_INCLUDE(preprocessor_t *pp, size_t begin) {
fail:
    return -1;
}

static int _process_IF(preprocessor_t *pp, size_t begin) {
fail:
    return -1;
}

/**
 * Handle the #define expression. There are two types, the simple and the function-like
 * macro. We need to add this symbol to our table, and then whenever we see the identifier
 * in the stream, replace the contents with the macro.
 */
static int _process_DEFINE(preprocessor_t *pp, size_t begin) {
    int err = 0;
    size_t index = begin;
    clextoken_t identifier;
    tokenlist_t args = {0,0};
    tokenlist_t body = {0,0};
    bool is_function = false;
    
    /* Skip the #define token */
    SKIP(pp, index, T_PRE_DEFINE);
    
    /*
     * Remove any trailing whitespace or comments after the
     * "#define" keyword
     */
    TRIM(pp,index);

    /*
     * Grab the identifier
     */
    identifier = TOK(pp,index);
    SKIP(pp, index, T_IDENTIFIER);
    /* do not TRIM() here */
    
    /*
     * If the immediate (no whitespace) next character is a parentheses,
     * then we have a function-like macro.
     */
    if (TOK(pp,index).id == T_PARENS_OPEN) {
        
        is_function = true;
        
        SKIP(pp, index, T_PRE_DEFINE);
        TRIM(pp,index);
        
        while (TOK(pp,index).id != T_PARENS_CLOSE) {
            if (TOK(pp,index).id == T_IDENTIFIER) {
                tokenlist_add(&args, TOK(pp,index));
                SKIP(pp,index,T_IDENTIFIER);
                TRIM(pp,index);
            }
            if (TOK(pp,index).id == T_PARENS_CLOSE)
                break;
            SKIP(pp, index, T_COMMA);
            TRIM(pp, index);
        }
        SKIP(pp, index, T_PARENS_CLOSE);
    }
    TRIM(pp, index);
    
    /* now add the body of the macro, which is just all the tokens until
     * a newline */
    while (TOK(pp, index).id != T_NEWLINE) {
        tokenlist_add(&body, TOK(pp,index));
        index++;
    }
    
    err = ppmacros_add(pp->macros, identifier, is_function, args, body);
    if (err)
        goto fail;
    return 0;

failed_unexpected:
    return C_ERR_TOK_UNEXPECTED;

fail:
    return err;
}

static int preproc_phase3_tokenize(preprocessor_t *pp) {
    
    while (pp->offset < pp->length) {
        struct clextoken_t token;
        token = preproc_next(pp);
        if (token.id == T_UNKNOWN) {
            printf("%s:%llu:%llu: unknown token\n", pp->filename, (unsigned long long)token.line_number, (unsigned long long)token.char_number);
            return -1;
        } else {
            printf("%s:%llu:%llu: %s \"%.*s\"\n", pp->filename, (unsigned long long)token.line_number, (unsigned long long)token.char_number,
                   clex_token_name(token),
                   (unsigned)token.string.length, token.string.string
                   );
            ;
        }
        tokenlist_add(&pp->tokens, token);
    }
    return 0;
}

static size_t _goto_end_of_line(preprocessor_t *pp, size_t index) {
    while (index < pp->tokens.count && TOK(pp, index).id != T_NEWLINE)
        index++;
    return index;
fail:
    return SIZE_MAX;
    
}

int preproc_phase4_preprocess(preprocessor_t *pp) {
    size_t i;
    size_t previous_line_number = 0;
    int err = 0;
    
    for (i=0; i<pp->tokens.count; i++) {
        clextoken_t token = pp->tokens.list[i];
        
        switch (token.id) {
            case T_UNKNOWN:
                goto fail;
            case T_WHITESPACE:
            case T_COMMENT:
                continue;
            case T_NEWLINE:
                previous_line_number = token.line_number;
                continue;
            case T_PRE_INCLUDE:
                err = _process_INCLUDE(pp, i);
                if (err)
                    goto fail;
                i = _goto_end_of_line(pp, i);
                break;
            case T_PRE_DEFINE:
                err = _process_DEFINE(pp, i);
                if (err)
                    goto fail;
                i = _goto_end_of_line(pp, i);
                break;
            case T_PRE_IF:
                err =_process_IF(pp, i);
                if (err)
                    goto fail;
                break;
            default:
                printf("%s:%llu:%llu: %s -> \"%.*s\"\n",
                       pp->filename,
                       (unsigned long long)token.line_number, (unsigned long long)token.char_number,
                       clex_token_name(token),
                       (unsigned)token.string.length,
                       token.string.string);
                ;
        }
        
        
        return 0;
    fail:
        return -1;


    }
    return 0;
}

static int preproc_phase1_charset(preprocessor_t *pp) {
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

static int preproc_phase2_linesplice(preprocessor_t *pp) {
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



int preproc_parse(preprocessor_t *pp) {
    int err;
    
    err = preproc_phase1_charset(pp);
    if (err)
        return err;
    
    err = preproc_phase2_linesplice(pp);
    if (err)
        return err;
    
    err = preproc_phase3_tokenize(pp);
    if (err)
        return err;
    
    err = preproc_phase4_preprocess(pp);
    if (err)
        return err;
    
    return 0; /* success */
}
