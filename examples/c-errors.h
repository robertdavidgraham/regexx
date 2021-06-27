#ifndef C_ERRORS_H
#define C_ERRORS_H

enum {
    C_ERR_NONE, /* no error */
    C_ERR_TOK_UNEXPECTED, /* unexpected token */
    C_ERR_OUT_OF_MEMORY,
    C_ERR_PREPROC_REDEFINITION, /* already defined */
};

#endif

