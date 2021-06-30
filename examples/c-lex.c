
#define FOO 1*1
#define BAR(a,b) bar(a+2,b##x)
#include "c-lex.h"
#include "../src/regexx.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

struct clex_t {
    regexx_t *re;
};

static struct {
    const char *name;
    const char *value;
} clex_macros[] = {
    {"O",   "[0-7]"},
    {"D",   "[0-9]"},
    {"NZ",  "[1-9]"},
    {"L",   "[a-zA-Z_]"},
    {"A",   "[a-zA-Z_0-9]"},
    {"H",   "[a-fA-F0-9]"},
    {"HP",  "(0[xX])"},
    {"E",   "([Ee][+-]?{D}+)"},
    {"P",   "([Pp][+-]?{D}+)"},
    {"FS",  "(f|F|l|L)"},
    {"IS",  "(((u|U)(l|L|ll|LL)?)|((l|L|ll|LL)(u|U)?))"},
    {"CP",  "(u|U|L)"},
    {"SP",  "(u8|u|U|L)"},
    {"ES",  "(\\\\(['\"\\?\\\\abfn" "rtv]" "|[0-7]{1,3}|x[a-fA-F0-9]+))"},
    {"WS",  "[ \\t\\v\\n\\f\\r]"},
    {"WS2", "[ \\t\\v\\f\\r]"},
    {"SPLICE","\\\\[\\r]*[\\n]"},
    {0,0}
};

static struct {
    size_t id;
    const char *name;
} token_names[] = {
    {T_KEYWORD, "KEYWORD"},
    {T_IDENTIFIER, "IDENTIFIER"},
    {T_INTEGER, "INTEGER"},
    {T_FLOAT, "FLOAT"},
    {T_STRING, "STRING"},
    {T_OP, "OPERATOR"},
    {T_WHITESPACE, "\" \""},
    {T_COMMENT, "/* */"},
    {T_NEWLINE, "\"\\n\""},
    {T_COMMA, ","},
    {T_PARENS_OPEN, "("},
    {T_PARENS_CLOSE, ")"},
    {T_ELLIPSES, "..."},

    {T__POUND, "#"},
    {T__POUNDPOUND, "##"},
    {T__DEFINE, "define"},
    {T__DEFINEFUNC, "define"},
    {T__INCLUDE, "include"},
    {T__IFDEF, "ifdef"},
    {T__IFNDEF, "ifndef"},
    {T__IF, "if"},
    {T__ELIF, "elif"},
    {T__ELSE, "else"},
    {T__ENDIF, "endif"},
    {T__LINE, "line"},
    {T__UNDEF, "undef"},
    {T__ERROR, "error"},
    {T__WARNING, "warning"},
    {T__PRAGMA, "pragma"},
    {T__DEFINED, "defined"},

    {0,0}
};

bool clex_tokens_are_equal(const clextoken_t lhs, clextoken_t rhs) {
    if (lhs.id != rhs.id)
        return false;
    switch (lhs.id) {
        case T_WHITESPACE:
        case T_COMMENT:
            /* all whitespace is the same */
            return true;
        default:
            if (lhs.string.length != rhs.string.length)
                return false;
            return memcmp(lhs.string.string, rhs.string.string, rhs.string.length) == 0;
    }
}
const char *clex_tokenid_name(int token_id) {
    size_t i;
    for (i=0; token_names[i].name; i++) {
        if (token_id == token_names[i].id) {
            return token_names[i].name;
        }
    }
    return "(unknown)";
}
const char *clex_token_name(struct clextoken_t token) {
    size_t i;
    for (i=0; token_names[i].name; i++) {
        if (token.id == token_names[i].id) {
            return token_names[i].name;
        }
    }
    return "(unknown)";
}
static struct {
    size_t id;
    const char *pattern;
} clex_exp[] = {
    {T_OP, "\\*"},

    //{T_PRE_INCLUDE, "#{WS2}*include{WS2}*\"[^\\n\"]+\""},
    //{T_PRE_INCLUDE, "#{WS2}*include{WS2}*<[^\\n>]+>"},
    {T_NEWLINE, "\\n"},
    //{T_WHITESPACE, "{WS2}*\\{WS2}*\n{WS2}*"},
    {T_WHITESPACE, "{WS2}+"},
    {T_WHITESPACE, "{WS2}*({SPLICE}+{WS2}*)+"},
#if 0
    {T_PRE_DEFINE, "#{WS2}*define"},
    {T_PREPROCESSOR, "#{WS2}*else"},
    {T_PREPROCESSOR, "#{WS2}*endif"},
    {T_PREPROCESSOR, "#{WS2}*error"},
    {T_PREPROCESSOR, "#{WS2}*if"},
    {T_PREPROCESSOR, "#{WS2}*ifdef"},
    {T_PREPROCESSOR, "#{WS2}*ifndef"},
    {T_PREPROCESSOR, "#{WS2}*line"},
    {T_PREPROCESSOR, "#{WS2}*pragma"},
    {T_PREPROCESSOR, "#{WS2}*undef"},
#endif
    {T_INTEGER, "{HP}{H}+{IS}?"}, /* hex integer */
    {T_INTEGER, "{NZ}{D}*{IS}?"},
    {T_INTEGER, "0{O}*{IS}?"},             /* octal integer */
    {T_INTEGER, "{CP}?'([^'\\\\n]|{ES})+'"},   /* character constant */
    {T_FLOAT, "{D}+{E}{FS}?"},
    {T_FLOAT, "{D}*\\.{D}+{E}?{FS}?"},
    {T_FLOAT, "{D}+\\.{E}?{FS}?"},
    {T_FLOAT, "{HP}{H}+{P}{FS}?"},
    {T_FLOAT, "{HP}{H}*\\.{H}+{P}{FS}?"},
    {T_FLOAT, "{HP}{H}+\\.{P}{FS}?"},
    {T_STRING, "({SP}?\\\"([^\"\\n]|{ES})*\\\"{WS}*)+"}, /* string */
    {T_KEYWORD, "auto"},
    {T_KEYWORD, "break"},
    {T_KEYWORD, "case"},
    {T_KEYWORD, "char"},
    {T_KEYWORD, "const"},
    {T_KEYWORD, "continue"},
    {T_KEYWORD, "default"},
    {T_KEYWORD, "do"},
    {T_KEYWORD, "double"},
    {T_KEYWORD, "else"},
    {T_KEYWORD, "enum"},
    {T_KEYWORD, "extern"},
    {T_KEYWORD, "float"},
    {T_KEYWORD, "for"},
    {T_KEYWORD, "goto"},
    {T_KEYWORD, "if"},
    {T_KEYWORD, "inline"},
    {T_KEYWORD, "int"},
    {T_KEYWORD, "long"},
    {T_KEYWORD, "register"},
    {T_KEYWORD, "restrict"},
    {T_KEYWORD, "return"},
    {T_KEYWORD, "short"},
    {T_KEYWORD, "signed"},
    {T_KEYWORD, "sizeof"},
    {T_KEYWORD, "static"},
    {T_KEYWORD, "struct"},
    {T_KEYWORD, "switch"},
    {T_KEYWORD, "typedef"},
    {T_KEYWORD, "union"},
    {T_KEYWORD, "unsigned"},
    {T_KEYWORD, "void"},
    {T_KEYWORD, "volatile"},
    {T_KEYWORD, "while"},
    {T_KEYWORD, "_Alignas"},
    {T_KEYWORD, "_Alignof"},
    {T_KEYWORD, "_Atomic"},
    {T_KEYWORD, "_Bool"},
    {T_KEYWORD, "_Complex"},
    {T_KEYWORD, "_Generic"},
    {T_KEYWORD, "_Imaginary"},
    {T_KEYWORD, "_Noreturn"},
    {T_KEYWORD, "_Static_assert"},
    {T_KEYWORD, "_Thread_local"},
    {T_KEYWORD, "__func__"},
    {T_ELLIPSES, "\\.\\.\\."},
    {T_OP, ">>="},
    {T_OP, "<<="},
    {T_OP, "\\+="},
    {T_OP, "-="},
    {T_OP, "\\*="},
    {T_OP, "/="},
    {T_OP, "%="},
    {T_OP, "&="},
    {T_OP, "^="},
    {T_OP, "\\|="},
    {T_OP, ">>"},
    {T_OP, "<<"},
    {T_OP, "\\+\\+"},
    {T_OP, "--"},
    {T_OP, "->"},
    {T_OP, "&&"},
    {T_OP, "\\|\\|"},
    {T_OP, "<="},
    {T_OP, ">="},
    {T_OP, "=="},
    {T_OP, "!="},
    {T_OP, ";"},
    {T_OP, "\\{"},
    {T_OP, "<%"},
    {T_OP, "\\}"},
    {T_OP, "%>"},
    {T_COMMA, ","},
    {T_OP, ":"},
    {T_OP, "="},
    {T_PARENS_OPEN, "\\("},
    {T_PARENS_CLOSE, "\\)"},
    {T_OP, "\\["},
    {T_OP, "<:"},
    {T_OP, "\\]"},
    {T_OP, ":>"},
    {T_OP, "\\."},
    {T_OP, "&"},
    {T_OP, "!"},
    {T_OP, "~"},
    {T_OP, "-"},
    {T_OP, "\\+"},
    {T_OP, "/"},
    {T_OP, "%"},
    {T_OP, "<"},
    {T_OP, ">"},
    {T_OP, "^"},
    {T_OP, "\\|"},
    {T_OP, "\\?"},
    {T__POUND, "#"},
    {T__POUNDPOUND, "##"},
    {T_IDENTIFIER, "{L}{A}*"}, /* identifier */
    {T_COMMENT, "\\/\\*.*?\\*\\/"}, /* *************************/
    {T_COMMENT, "\\/\\/.*?(?=\\n)"},
    {T_COMMENT, "\\/\\/([^\\n]*?{SPLICE})+[^\\n]*?(?=\\n)"},
    {0}
};


clex_t *clex_create(void) {
    clex_t *clex;
    size_t i;
    
    clex = calloc(1, sizeof(*clex));
    if (clex == NULL)
        abort();

    /*
     * Create a lexer
     */
    clex->re = regexx_create(0);
    
    /*
     * Add some macros to make regexes simpler
     */
    for (i=0; clex_macros[i].name; i++) {
        regexx_add_macro(clex->re, clex_macros[i].name, clex_macros[i].value);
    }

    /*
     * Add all the regex patterns for tokens
     */
    for (i=0; clex_exp[i].pattern; i++) {
        int err;
        err = regexx_add_pattern(clex->re, clex_exp[i].pattern, clex_exp[i].id, 0);
        if (err) {
            fprintf(stderr, "[-]%u: %s\n", (unsigned)i, regexx_get_error_msg(clex->re));
            fprintf(stderr, "[-] pattern = %s\n", clex_exp[i].pattern);
            goto fail;
        }
        //printf("%s\n", regexx_print(clex->re, 0, 0, 0));
    }
    
    return clex;
    
fail:
    clex_free(clex);
    return NULL;
}

clextoken_t clex_next(clex_t *clex, const char *buf, size_t *offset, size_t length) {
    clextoken_t result;
    regexxtoken_t token;

    /* Get the next token */
    token = regexx_lex_token(clex->re, buf, offset, length);

    /* if found, simply return that token */
    if (token.id != REGEXX_NOT_FOUND) {
        result.id = token.id;
        result.string.string = token.string;
        result.string.length = token.length;
        result.line_number = token.line_number;
        result.char_number = token.char_number;
        return result;
    }

    /* Kludge: if end of input, pretend there's a newline at the end of the
     * file. */
    if (*offset >= length) {
        result.id = T_NEWLINE;
        result.string.string = "\n";
        result.string.length = 1;
        return result;
    }

    /* This is some character that isn't a valid token. We'll get these
     * when processing `#if 0 ...` sections that are otherwise skipped. */
    result.id = T__BADCHAR;
    result.string.string = buf + *offset;
    result.string.length = 1;
    (*offset)++;

    return result;
}


void clex_free(clex_t *clex) {
    regexx_free(clex->re);
    free(clex);
}

void clex_push(clex_t *clex) {
    regexx_lex_push(clex->re);
}

void clex_pop(clex_t *clex) {
    regexx_lex_pop(clex->re);
}
