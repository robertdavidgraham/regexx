#include "../src/regexx.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>


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
    {"ES",  "(\\\\(['\"\\?\\\\abfnrtv]|[0-7]{1,3}|x[a-fA-F0-9]+))"},
    {"WS",  "[ \\t\\v\\n\\f\\r]"},
    {"WS2", "[ \\t\\v\\f\\r]"},
    {0,0}
};

enum {
    T_KEYWORD,
    T_IDENTIFIER,
    T_INTEGER,
    T_FLOAT,
    T_STRING,
    T_OPERATOR,
    T_WHITESPACE,
    T_COMMENT,
    T_PREPROCESSOR,
    T_NEWLINE,
    T_PRE_DEFINE,
    T_PRE_INCLUDE,
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
    {T_OPERATOR, "OPERATOR"},
    {T_WHITESPACE, "\" \""},
    {T_COMMENT, "/* */"},
    {T_PREPROCESSOR, "#PREPROC"},
    {T_NEWLINE, "\"\\n\""},
    {T_PRE_DEFINE, "#define"},
    {T_PRE_INCLUDE, "#include"},
    {0,0}
};
const char *token_name(struct regexxtoken_t token) {
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
    {T_PRE_INCLUDE, "#{WS2}*include{WS2}*\".+\""},
    {T_PRE_INCLUDE, "#{WS2}*include{WS2}*<.+>"},
    {T_NEWLINE, "\\n"},
    {T_WHITESPACE, "{WS2}+"},
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
    {T_IDENTIFIER, "{L}{A}*"}, /* identifier */
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
    {T_STRING, "({SP}?\\\"([^\"\\\\n]|{ES})*\\\"{WS}*)+"}, /* string */
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

    {0}
};


int parse_file(regexx_t *re, FILE *fp, const char *filename) {
    struct regexxtoken_t token;
    struct stat st = {0};
    char *buf;
    size_t length;
    ssize_t bytes_read;
    size_t offset = 0;
    
    /* Discover the size of the file */
    if (fstat(fileno(fp), &st) != 0 || st.st_size == 0) {
        return -1;
    }
    length = st.st_size;
    
    /* Allocate a buffer to hold the entire file in memory.
     * TODO: we should iterate over chunks of the file instead
     * TODO: of holding the entire thing in memory */
    buf = malloc(length+1);
    
    /* Read in the file */
    bytes_read = fread(buf, 1, length, fp);
    if (bytes_read < 0 || bytes_read < length) {
        free(buf);
        return -1;
    }
    buf[length] = '\0'; /* nul terminate for debugging */
    
    /*
     * Tokenize the file
     */
    token = regexx_lex_token(re, buf, &offset, length);
    while (offset<length) {
        
        switch (token.id) {
            case REGEXX_NOT_FOUND:
                goto fail;
            case T_PRE_INCLUDE:
                /* Remove any trailing whitespace or comments */
                do {
                    token = regexx_lex_token(re, buf, &offset, length);
                } while (token.id == T_WHITESPACE || token.id == T_COMMENT);
                
                /* It must end in a newline */
                if (token.id != T_NEWLINE)
                    goto fail;
                
                /* skip the newline */
                token = regexx_lex_token(re, buf, &offset, length);
                break;
            case T_WHITESPACE:
            case T_COMMENT:
            case T_NEWLINE:
                /* skip this token */
                token = regexx_lex_token(re, buf, &offset, length);
                break;
            default:
                printf("%s:%llu:%llu: %s -> \"%.*s\"\n",
                       filename,
                       (unsigned long long)token.line_number, (unsigned long long)token.line_offset,
                       token_name(token),
                       (unsigned)token.length,
                       token.string);
                ;
        }
    }
    
    
    return 0;
fail:
    printf("%s:%llu:%llu: unknown token\n", filename, (unsigned long long)token.line_number, (unsigned long long)token.line_offset);
    return -1;
}

int main(int argc, char *argv[]) {
    int i;
    regexx_t *re;
    
    printf("cwd = %s\n", getcwd(0,0));
    /*
     * Initialize the regex with lex tokens
     */
    re = regexx_create(0);
    for (i=0; clex_macros[i].name; i++) {
        regexx_add_macro(re, clex_macros[i].name, clex_macros[i].value);
    }
    for (i=0; clex_exp[i].pattern; i++) {
        int err;
        err = regexx_add_pattern(re, clex_exp[i].pattern, clex_exp[i].id, 0);
        if (err) {
            fprintf(stderr, "[-]%u: %s\n", (unsigned)i, regexx_get_error_msg(re));
        }
        //fprintf(stderr, "[+] %u = %s\n", (unsigned)clex_exp[i].id, regexx_print(re, i, 0, 0));
    }
    
  
    for (i=1; i<argc; i++) {
        const char *filename = argv[i];
        FILE *fp = fopen(filename, "rb");
        int err;
        
        if (fp == NULL) {
            perror(argv[i]);
            continue;
        }
        err = parse_file(re, fp, filename);
        if (err)
            perror(filename);
        fclose(fp);
    }
}
