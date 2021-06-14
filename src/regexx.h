/*
    A regular expression library
 
    This is hand-written from scratch to serve primarily as a basis for
    lexical analysis (like `lex` or `flex`). However, instead of the technique
    of `lex` using an external `.l` file, this is intended to be used
    within a C program. The programmer just provides an array of all
    the lexical tokens.
 */
#ifndef REGEXX_H
#define REGEXX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

typedef struct regexx_t regexx_t;

#define REGEXX_NOT_FOUND SIZE_MAX

enum regexx_flags_t {
    REGEXX_LAZY = 0x00000010,
    REGEXX_IGNORECASE = 0x00000020,

};

/**
 * Create a regular-expression pattern matcher.
 * @param flags
 *  Flags that control how the pattern matching system will work, or `0` for
 * defaults
 * @return
 *  A pattern-matching machine that must eventually be freed with
 *  `regexx_free()`.
 */
regexx_t *regexx_create(unsigned flags);

/**
 * Free the resources allocated by `regexx_create()`, or other functions that
 * call it internally (like `regexx_compile_one()`.
 * @param re
 *  An object created by `regexx_create()`.
 */
void regexx_free(regexx_t *re);

/**
 * Add a macro that can be used when defining regular expressions.
 */
int regexx_add_macro(regexx_t *re, const char *name, const char *value);

/**
 * Add a regular-expression to the pattern matcher, and an ID of what will
 * be returned when that regular expression matches.
 * @param re
 *  A regular-expression engine newly created with `regexx_create()`.
 * @param pattern
 *  A nul-terminated string containing a regular expression, which contains
 *  the syntax of regular expressions.
 * @param id
 *  The identifier that will be returned when this pattern matches. Multiple
 *  patterns can be searched at the same time. This value cannot be zero.
 * @return
 *  0 on success, or a negative number on error
 */
int regexx_add_pattern(regexx_t *re, const char *pattern, size_t id, unsigned flags);

/**
 * Gets the regular expression, by `index`.
 * @param re
 *  A regex pattern-matching subsystem with compiled regex patterns.
 * @param index
 *  Which of the many patterns to retrieve, from [0..count]. If the index
 *  is too high, then the result of this function will be NULL, and
 *  `*id* will be set to REGEXX_NOT_FOUND.
 * @param id
 *  Receives the 'id' of the pattern when it was created with `regexx_add_pattern()`.
 * @param is_flag_shown
 *  Whether the configuration flags are shown, i.e., in the form "/(regex)/bgm"
 */
char *regexx_print(regexx_t *re, size_t index, size_t *id, bool is_flag_shown);

/**
 * Using compiled regex patterns, match an input string.
 */
size_t regexx_match(regexx_t *re, const char *input, size_t in_length, size_t *out_offset, size_t *out_length);

/**
 * Retrieve the latest error message. Call this if one of the other functions returns
 * an error.
 */
const char *regexx_get_error_msg(regexx_t *re);

#ifdef __cplusplus
}
#endif
#endif
