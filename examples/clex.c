#include "../src/regexx.h"

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
    {"WS",  "[ \\t\\v\\n\\f]"},
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
};
static struct {
    size_t id;
    const char *pattern;
} clex_exp[] = {
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
    {0}
};

void parse_file(FILE *fp) {
}

int main(int argc, char *argv[]) {
    int i;
    regexx_t *re;
    
    re = regexx_create(0);

    for (i=1; i<argc; i++) {
        FILE *fp = fopen(argv[i], "rb");
        if (fp == NULL) {
            perror(argv[i]);
            continue;
        }
        parse_file(re, fp);
        fclose(fp);
    }
}