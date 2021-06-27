#ifndef C_LEX_H
#define C_LEX_H
#include <stdint.h>

enum {
    T_KEYWORD,
    T_IDENTIFIER,
    T_INTEGER,
    T_FLOAT,
    T_STRING,
    T_OP,
    T_WHITESPACE,
    T_COMMENT,
    T_PREPROCESSOR,
    T_NEWLINE,
    T_PRE_DEFINE,
    T_PRE_INCLUDE,
    T_PRE_IF,
    T_PRE_ENDIF,
    T_PRE_UNDEF,
    T_PRE_STRINGIZING,
    T_PRE_TOKENPASTING,
    T_COMMA,
    T_PARENS_OPEN,
    T_PARENS_CLOSE,
    T_UNKNOWN = SIZE_MAX
};

struct clex_t;
typedef struct clex_t clex_t;

typedef struct clextokenstring_t {
    size_t length;
    const char *string;
} clextokenstring_t;
typedef struct clextoken_t {
    size_t id;
    clextokenstring_t string;
    size_t line_number;
    size_t char_number;
} clextoken_t;

clex_t *clex_create(void);
void clex_free(clex_t *clex);

clextoken_t clex_next(clex_t *clex, const char *buf, size_t *offset, size_t length);

const  char *clex_token_name(const clextoken_t token);

void clex_push(clex_t *clex);
void clex_pop(clex_t *clex);


#endif

