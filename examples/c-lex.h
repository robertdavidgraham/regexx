#ifndef C_LEX_H
#define C_LEX_H
#include <stdint.h>
#include <stdbool.h>

enum clextokenid_t {
    T_KEYWORD,
    T_IDENTIFIER,
    T_INTEGER,
    T_FLOAT,
    T_STRING,
    T_OP,
    T_WHITESPACE,
    T_COMMENT,
    T_NEWLINE,
    T_COMMA,
    T_PARENS_OPEN,
    T_PARENS_CLOSE,
    T_ELLIPSES,

    /* preprocessing tokens */
    T__POUND,
    T__POUNDPOUND,
    T__DEFINE,
    T__DEFINEFUNC,
    T__INCLUDE,
    T__IFDEF,
    T__IFNDEF,
    T__IF,
    T__ELIF,
    T__ELSE,
    T__ENDIF,
    T__LINE,
    T__UNDEF,
    T__ERROR,
    T__WARNING,
    T__PRAGMA,
    T__DEFINED,
    T__BADCHAR,

    T_UNKNOWN = SIZE_MAX
};

struct clex_t;
typedef struct clex_t clex_t;

typedef struct clextokenstring_t {
    size_t length;
    const char *string;
} clextokenstring_t;
typedef struct clextoken_t {
    enum clextokenid_t id;
    clextokenstring_t string;
    size_t line_number;
    size_t char_number;
} clextoken_t;

clex_t *clex_create(void);
void clex_free(clex_t *clex);

clextoken_t clex_next(clex_t *clex, const char *buf, size_t *offset, size_t length);

const  char *clex_token_name(const clextoken_t token);
const  char *clex_tokenid_name(int token_id);

bool clex_tokens_are_equal(const clextoken_t lhs, clextoken_t rhs);

void clex_push(clex_t *clex);
void clex_pop(clex_t *clex);


#endif

