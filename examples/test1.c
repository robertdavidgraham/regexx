#include "regexx.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>


int regex_selftest(void) {
#define ULL "(" "([Uu]?[Ll]?[Ll]?)" "|([Ll]?[Ll]?[Uu]?)" ")?"
#define identifier "[A-Z_a-z]\\w*"
#define int_hex "0[Xx][0-9A-Fa-f]+" ULL
#define int_oct "0[0-7]+" ULL
#define int_dec "[1-9]\\d*" ULL
#define string_lit "(L|u|U|u8)?\"[^\"]*\""
    
#define char_esc1 "(\\'" "|\\\"" "|\\?" "|\\\\" "|\\a" "|\\b" "|\\f" "|\\n" "|\\r" "|\\t" "|\\v)"
#define char_esc2 "((\\0x[0-9A-Fa-f]+)|(\\0[0-7]+)|(\\[Uu][[0-9A-Fa-f]+))"
#define char_ut8 "("

#define float1 "\\d+[EPep][+\\-]?\\d+"
#define float2 "\\.\\d+([EPep][+\\-]?\\d+)?"
#define float3 "\\d+\\.\\d*([EPep][+\\-]?\\d+)?"
#define float4 "0x[0-9A-Fa-f]+\\.[0-9A-Fa-f]*([EPep][+\\-]?\\d+)?"
#define num_float "(" "(" float1 ")|(" float2 ")|(" float3 ")|(" float4 "))[FLfl]?"
    
    static const struct testcase_t {
        const char *pattern;
        const char *text;
        size_t offset;
        size_t length;
    } testcases[] = {
        //{float1, "ex1 = 0x1.2p3", 6, 7},
        {"(" float4 ")[FLfl]?", "0x1.2p3", 0, 7},
        {float2, "float2 = 0x1.2p3", 12, 4},
        {float3, "float3 = 0x1.2p3", 11, 5},
        {float4, "float4 = 0x1.2p3", 9, 7},
        {num_float, "ex1 = 0x1.2p3", 6, 7},

        {"(.*?at)",  "The fat cat sat on the mat.", 0, 7},
        {"(.*at)",  "The fat cat sat on the mat.", 0, 26},
        {".*at",  "fat", 0, 3},
        {"c(def)*g", "abcghi", 2, 2},
        
        {"abc", "xabcx", 1, 3},
        {"[Hh]ello", "'hello'", 1, 5},
        {"a|b", "foobar", 3, 1},
        {"a|b", "foodar", 4, 1},
        {"x(a|b)*y", "xxxabbbaabyyy", 2, 9},
        {"cat|dog|fox", "The quick brown fox jumps over ", 16, 3},
        {"cat|dog|fox", "The quick brown dog jumps over ", 16, 3},
        {"cat|dog|fox", "The quick brown cat jumps over ", 16, 3},
        {"c(def)?g", "abcdefghi", 2, 5},
        {"c(def)*g", "abcghi", 2, 2},
        
        {"c(def)+g", "abcdefdefghi", 2, 8},
        {"[Hh]ello [Ww]orld\\s*[!]?", "ahem.. 'hello world !' ..", 8, 13},
        {"d[!]?", "hello world!", 10, 2},
        {"a\\s*b", "xabx", 1, 2},
        {"a\\s*b", "xa bx", 1, 3},
        {"a\\s*b", "xa  bx", 1, 4},
        {"a\\s*b", "xa   bx", 1, 5},
        {num_float, "ex1 = 0x1.2p3", 6, 7},
        {num_float, "ex2 = 0x1.FFFFFEp128f", 6, 15},
        {string_lit, "str = \"hello\\n\" world", 6, 9},
        {num_float, "pi = 3.141592653589793L", 5, 18},
        
        {num_float, "num = 12e9f", 6, 5},
        {int_oct, "A is 65ULl 0x41 0101U \n", 16, 5},
        {int_dec, "A is 65ULl 0x41 0101U \n", 5, 5},
        {int_hex, "A is 65ULl 0x41 0101U \n", 11, 4},
        {identifier, " x += 3; \n", 1, 1},
        {identifier, " Foo += 3; \n", 1, 3},
        {identifier, " F00 += 3; \n", 1, 3},
        {identifier, " 900 BAR \n", 5, 3},
        {0, 0}};
    size_t i;

    for (i=0; testcases[i].pattern; i++) {
        size_t match_length=0;
        size_t match_offset=0;
        const struct testcase_t *expected = &testcases[i];
        regexx_t *re;
        int err;
        size_t id;
        
        re = regexx_create(0);
        err = regexx_add_pattern(re, expected->pattern, i, 0);
        if (err) {
            fprintf(stderr, "[-]%u: %s\n", (unsigned)i, regexx_get_error_msg(re));
            continue;
        }

        /*regexx_print(re, stderr, 0);
        fprintf(stderr, "\n");*/
        id = regexx_match(re, expected->text, SIZE_MAX, &match_offset, &match_length);
        if (id == REGEXX_NOT_FOUND || match_offset != expected->offset || match_length != expected->length) {
            fprintf(stderr, "[-]%2u: \"%s\"\n", (unsigned)i, regexx_print(re, 0, 0, 0));
            fprintf(stderr, "[%c] id=%u, expected=%u\n",
                    (id == 1)?'+':'-',
                    (unsigned)id, (unsigned)1);
            if (id > 0 && id != SIZE_MAX) {
                fprintf(stderr, "[ ] %s\n", expected->text);
                fprintf(stderr, "[ ] %.*s%.*s\n", (unsigned)match_offset, "             ", (unsigned)match_length, expected->text + match_offset);
                fprintf(stderr, "[%c] offset=%u, expected=%u\n",
                        (match_offset == expected->offset)?'+':'-',
                        (unsigned)match_offset, (unsigned)expected->offset);
                fprintf(stderr, "[%c] length=%u, expected=%u\n",
                        (match_length == expected->length)?'+':'-',
                        (unsigned)match_length, (unsigned)expected->length);
            }
            return 1;
        }
        
        regexx_free(re);
    }
    return 0;
}



static int selftest_parses(void) {
    static const struct parsecase_t {
        const char *pattern;
        const char *expected;
    } parsecases[] = {
        {identifier, identifier},
        {string_lit, string_lit},
        {num_float, num_float},
        {int_hex, int_hex},
        {int_oct, int_oct},
        {int_dec, int_dec},
        {"a|b", "a|b"},
        {"abc(pdq|xyz)*def", "abc(pdq|xyz)*def"},
        {"abc(def)+efg", "abc(def)+efg"},
        {"abc.+def", "abc.+def"},
        {"[\\t\\v\\f ]+", "[\\t\\v\\f ]+"},
        {"[a-fA-F0-9]", "[0-9A-Fa-f]"},
        {"[^a-zA-Z]", "[^A-Za-z]"},
        {"\\$\\d+\\.d+", "\\$\\d+\\.d+"}, /* match all $23.00 */
        {"a[bc]", "a[bc]"},
        {"abc*", "abc*"},
        {"^The", "^The"},
        {0,0}
    };
    size_t i;
    
 
    
    for (i=0; parsecases[i].pattern; i++) {
        char *buf;
        regexx_t *re;
        
        re = regexx_create(0);
        regexx_add_pattern(re, parsecases[i].pattern, 1, 0);
        buf = regexx_print(re, 0, 0, 0);
        //fprintf(stderr, "%s\n", buf);
        regexx_free(re);
        
        
        if (strcmp(buf, parsecases[i].expected) != 0) {
            fprintf(stderr, "[-] parse case %u failed\n", (unsigned)i);
            fprintf(stderr, "[-] regex:    %s\n", parsecases[i].pattern);
            fprintf(stderr, "[-] expected: %s\n", parsecases[i].expected);
            fprintf(stderr, "[-] found:    %s\n", buf);
            return 1;
        }
        free(buf);
        
    }
    
    return 0;
}

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

static struct {
    const char *pattern;
} clex_exp[] = {
    {"{L}{A}*"}, /* identifier */
    {"{HP}{H}+{IS}?"}, /* hex integer */
    {"{NZ}{D}*{IS}?"},
    {"0{O}*{IS}?"},             /* octal integer */
    {"{CP}?'([^'\\\\n]|{ES})+'"},   /* character constant */
    {"{D}+{E}{FS}?"},
    {"{D}*\\.{D}+{E}?{FS}?"},
    {"{D}+\\.{E}?{FS}?"},
    {"{HP}{H}+{P}{FS}?"},
    {"{HP}{H}*\\.{H}+{P}{FS}?"},
    {"{HP}{H}+\\.{P}{FS}?"},
    {"({SP}?\\\"([^\"\\\\n]|{ES})*\\\"{WS}*)+"}, /* string */
    {0}
};

static int selftest_macros(void) {
    regexx_t *re = regexx_create(0);
    size_t i;
    int result = 0;
    
    for (i=0; clex_macros[i].name; i++) {
        regexx_add_macro(re, clex_macros[i].name, clex_macros[i].value);
    }
    for (i=0; clex_exp[i].pattern; i++) {
        int err;
        err = regexx_add_pattern(re, clex_exp[i].pattern, i, 0);
        if (err) {
            fprintf(stderr, "[-]%u: %s\n", (unsigned)i, regexx_get_error_msg(re));
            result++;
        }
        fprintf(stderr, "%s\n", regexx_print(re, i, 0, 0));
    }
    return 0;
}
int main(int argc, char *argv[]) {
    int x = 0;

    x += selftest_macros();
    
    x += selftest_parses();
    

    x += regex_selftest();
    if (x == 0) {
        fprintf(stderr, "[+] selftest succeeded\n");
        return 0;
    } else {
        fprintf(stderr, "[-] selftest failed\n");
        return 1;
    }
    return 0;
}
