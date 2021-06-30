
#define TEST 1
/*
    Regexx
 
 Code organization:
    Each pattern is simply parsed left-to-right, pulling out
    subexpressions (nodes) as it goes. See `_node_parse_next()`.
 
    Likewise, matching a pattern proceeds simply left-to-right.
    See `_node_evail()`. Evaluation is straightforward
    NFA backtracking engine.
 
    We also can print out our parsed expressesion. See `_node_print()`.
 
    All three of these functions are just a large switch/case
    block with all the subexpresion (node) types. See `nodetype_t`
    for a list of all nodes.
 */
#include "regexx.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#define strdup _strdup
#endif

/** All the possible sub-expresison types.
 * Some are artificial used for internal processing and won't be exposed externally.
 * Some combined multiple things, such as '+' equallying {1,} */
enum nodetype_t {
    T_UNKNOWN,
    T_ROOT,
    T_GROUP_START,  /* starts a new group, when ')' is found, changes to T_GROUP */

    T_ALTERNATION,  /* like "cat|dog" */
    T_QUANTIFIER,   /* {2,3} - number of times repeated, also '+', '*', '?' */
    T_GROUP,        /* a capture group, like "(abc)+" */
    T_DOT_ALL,      /* matches any character */
    T_DOT_NONEWLINE,/* matches any character but '\r' and '\n' */
    T_ANCHOR_BEGIN, /* '^' at start of regex */
    T_ANCHOR_END,   /* '$' at end of regex */
    T_STRING,       /* a specific character */
    T_CHARCLASS,    /* a character class, like [abc] or [^0-9] */
    
    T_TRUE          /* return TRUE, terminates chains when everything in the chain matched */
};


/** Stores character-classes as a set of bit flags, 256 flags in total,
 * or 32-bytes. This makes evaluation really quick, requiring a single
 * bit lookup. Also, storage of large groups is efficient. */
typedef struct charclass_t {
    uint64_t list[4];
} charclass_t;

/**
 * A single node in the regex syntax tree.
 */
typedef struct node_t {
    enum nodetype_t type;
    union {
        charclass_t charclass;
        struct {
            struct node_t *child;
            size_t min;
            size_t max;
            bool is_lazy:1;
        } quantifier;
        struct {
            struct node_t *child;
        } alternation;
        struct {
            struct node_t *child;
            bool is_lookahead:1;
            bool is_inverted:1;
            bool is_noncapturing:1;
        } group;
        struct {
            unsigned char length;
            bool is_case_insensitive:1;
            char chars[56];
        } string;
    };
    struct node_t *next;
    struct node_t *prev;
} node_t;

typedef struct macro_t {
    char *name;
    char *value;
} macro_t;

typedef struct buf_t {
    char *string;
    size_t length;
} buf_t;

typedef struct fileoffsets_t {
    size_t line_number;
    size_t char_number;
    struct fileoffsets_t *next;
} fileoffsets_t;

typedef struct regexx_t {
    /* For parsing regex patterns: the head of the chain we
     * are currently parsing. */
    node_t *head;
    
    /* For parsing regex patterns: the tail of the current
     * chain we are working on*/
    node_t *tail;
    
    
    bool is_dot_match_newline;
    
    /* Lex-style macros that can be used in regular expressions */
    macro_t *macros;
    size_t macro_count;
    
    /* When an error happens, the error message goes here. Use
     * `regexx_get_error_msg()` to retrieve */
    buf_t error_msg;
    
    /* The list of all the patterns we know about */
    struct {
        node_t *head;
        size_t id;
    } *patterns;
    size_t pattern_count;
    
    fileoffsets_t offsets;
} regex_t;

void regexx_lex_push(regexx_t *re) {
    fileoffsets_t *o = malloc(sizeof(*o));
    o->line_number = re->offsets.line_number;
    o->char_number = re->offsets.char_number;
    o->next = re->offsets.next;
    re->offsets.line_number = 1;
    re->offsets.char_number = 0;
    re->offsets.next = 0;
}
void regexx_lex_pop(regexx_t *re) {
    fileoffsets_t *o = re->offsets.next;
    if (o == NULL) {
        fprintf(stderr, "[-] regexx_lex_pop: error\n");
        return;
    }
    re->offsets.line_number = o->line_number;
    re->offsets.char_number = o->char_number;
    re->offsets.next = o;
    free(o);
}

static const char *_node_print(node_t *node, buf_t *buf);

/**
 * Like `sprintf()`, but appends strings to a buffer. This is used
 * for two purposes:
 *  - printing regexp's after they've been parsed, so that the
 *    user can see how they've been interpretted
 *  - for formatting error messages when things go wrong.
 */
static void _appendf(buf_t *buf, const char *fmt, ...) {
    va_list ap;
    int len;
    
    va_start(ap, fmt);
    len = vsnprintf(0, 0, fmt, ap);
    if (len < 0)
        fprintf(stderr, "[-] vsnprintf() returned error %s\n", strerror(errno));
    va_end(ap);
    if (len < 0)
        return;
    buf->string = realloc(buf->string, buf->length + len + 1);
    if (buf->string == NULL) {
        buf->length = 0;
        return;
    }
    va_start(ap, fmt);
    len = vsnprintf(buf->string + buf->length, len + 1, fmt, ap);
    va_end(ap);
    
    buf->length += len;
}

static void _error_msg(regexx_t *re, char *fmt, ...) {
    va_list ap;
    int len;
    buf_t *buf = &re->error_msg;
    
    buf->length = 0;
    
    va_start(ap, fmt);
    len = vsnprintf(buf->string + buf->length, 0, fmt, ap);
    va_end(ap);
    if (len < 0)
        return;
    buf->string = realloc(buf->string, buf->length + len + 1);
    if (buf->string == NULL) {
        buf->length = 0;
        return;
    }
    va_start(ap, fmt);
    len = vsnprintf(buf->string + buf->length, len + 1, fmt, ap);
    va_end(ap);
    
    buf->length += len;
}


static macro_t *_macro_new(struct regexx_t *re, const char *name, const char *value) {
    macro_t *macro;
    
    re->macros = realloc(re->macros, (re->macro_count+1) * sizeof(macro_t));
    macro = &re->macros[re->macro_count++];
    macro->name = strdup(name);
    macro->value = strdup(value);
    return macro;
}

static macro_t *_macro_lookup(struct regexx_t *re, const char *name, size_t length) {
    size_t i;
    for (i=0; i<re->macro_count; i++) {
        macro_t *macro = &re->macros[i];
        if (strlen(macro->name) != length)
            continue;
        if (memcmp(macro->name, name, length) == 0)
            return macro;
    }
    return NULL;
}

int regexx_add_macro(regexx_t *re, const char *name, const char *value) {
    size_t i;
    

    /*
     * verify name
     */
    if (!isalpha(name[0]&0xFF) && name[0] != '-') {
        return -1;
    }
    for (i=0; name[i]; i++) {
        if (!isalnum(name[0]&0xFF) && name[0] != '-') {
            return -1;
        }
    }

    _macro_new(re, name, value);
    return 0;
}


 /* [\n\r\t\v\f ] */
static const charclass_t _whitespace = {0x0000000100003e00ULL,0,0,0};
/* [A-Za-z0-9_] */
static const charclass_t _word = {0x03ff000000000000,0x07fffffe87fffffe,0,0};
/* [0-9] */
static const charclass_t _digits = {0x03ff000000000000,0,0,0};
/* [\s\S] */
static const charclass_t _dot_all = {~0ULL,~0ULL,~0ULL,~0ULL};

/** Tests if the specified character is in the class */
static bool _charclass_match_char(const charclass_t *charclass, unsigned c) {
    uint64_t x;
    uint64_t y;
    c &= 0xFF;
    x = charclass->list[c>>6];
    c &= 0x3f;

    y = x & (1ULL<<(uint64_t)c);

    return (x & y) != 0ULL;
}

/** Adds a character to the class */
static void _charclass_add_char(charclass_t *charclass, unsigned c) {
    uint64_t *x;
    c &= 0xFF;
    x= &charclass->list[c>>6];
    *x |= (1ULL<<(c&0x3fULL));
}

/** Inverts the class, as when [^..] is at the front. */
static charclass_t _invert(charclass_t charclass) {
    charclass_t result;
    result.list[0] = ~charclass.list[0];
    result.list[1] = ~charclass.list[1];
    result.list[2] = ~charclass.list[2];
    result.list[3] = ~charclass.list[3];
    return result;
}

/** Merge two character classes together. */
static charclass_t _charclass_merge(charclass_t lhs, charclass_t rhs) {
    charclass_t result;
    result.list[0] = lhs.list[0] | rhs.list[0];
    result.list[1] = lhs.list[1] | rhs.list[1];
    result.list[2] = lhs.list[2] | rhs.list[2];
    result.list[3] = lhs.list[3] | rhs.list[3];
    return result;
}

/** Tests if two charclasss are equal, having the same moembers */
static bool _charclass_is_equal(charclass_t lhs, charclass_t rhs) {
    return lhs.list[0] == rhs.list[0]
    && lhs.list[1] == rhs.list[1]
    && lhs.list[2] == rhs.list[2]
    && lhs.list[3] == rhs.list[3]
    ;
}

/** Counts the number of characters in the charclass */
static unsigned _charclass_count(charclass_t charclass) {
    size_t i;
    unsigned result = 0;
    for (i=0; i<4; i++) {
        uint64_t x = charclass.list[i];
        if (x == 0)
            continue;
        else {
            uint64_t j;
            for (j=0; j<64; j++) {
                if (x & (1ull<<j))
                    result++;
            }
        }
    }
    return result;
}
static char _charclass_first_char(charclass_t charclass) {
    size_t i;
    for (i=0; i<4; i++) {
        unsigned char c;
        uint64_t x = charclass.list[i];
        if (x == 0ULL)
            continue;
        for (c=i*64; c<(i+1)*64; c++) {
            if (_charclass_match_char(&charclass, c))
                return (char)c;
        }
    }
    return 0;
}

static unsigned _letter_run(charclass_t charclass, unsigned letter) {
    unsigned result = 0;
    while (isalpha(letter + result)) {
        if (!_charclass_match_char(&charclass, letter+result))
            break;
        result++;
    }
    return result;
}
static unsigned _digit_run(charclass_t charclass, unsigned letter) {
    unsigned result = 0;
    while (isdigit(letter + result)) {
        if (!_charclass_match_char(&charclass, letter+result))
            break;
        result++;
    }
    return result;
}

static void _charclass_print(charclass_t charclass, char *buf, size_t length) {
    unsigned c;
    size_t offset = 0;
    unsigned run;
    
    for (c=0; c<256; c++) {
        if (!_charclass_match_char(&charclass, c))
            continue;
        
        run = _letter_run(charclass, c);
        if (run == 0)
            run = _digit_run(charclass, c);
        
        if (run > 2) {
            offset += snprintf(buf+offset, length-offset, "%c-%c", c, c+run-1);
            c += run-1;
        } else if (c == 0) {
            offset += snprintf(buf+offset, length-offset, "\\0");
        } else if (strchr("^-[]\\", c)) {
            offset += snprintf(buf+offset, length-offset, "\\%c", c);
        } else if (strchr("\t\n\v\f\r", c)) {
            const char *foo = "**err**";
            switch (c) {
                case '\t': foo = "\\t"; break;
                case '\n': foo = "\\n"; break;
                case '\v': foo = "\\v"; break;
                case '\f': foo = "\\f"; break;
                case '\r': foo = "\\r"; break;
                default:
                    foo = "*err*";
                    break;
            }
            offset += snprintf(buf+offset, length-offset, "%s", foo);
        } else if (c <=26) {
            offset += snprintf(buf+offset, length-offset, "\\c%c", 'A'+c);
        } else if (isprint(c)) {
            offset += snprintf(buf+offset, length-offset, "%c", (char)c);
        } else {
            offset += snprintf(buf+offset, length-offset, "\\%03o", c);
        }
        
        if (offset + 1 >= length)
            break;
    }
    if (offset < length)
        buf[offset] = '\0';
    else
        memcpy(buf, "***error***", 12);

}


/**
 * Recursively called function to free all the nodes.
 */
static void _node_free(node_t *node) {
    if (node) {
        switch (node->type) {
            case T_QUANTIFIER:
                _node_free(node->quantifier.child);
                break;
            case T_ALTERNATION:
                _node_free(node->alternation.child);
                break;
            case T_GROUP:
                _node_free(node->group.child);
                break;
            default:
                ;
        }
        _node_free(node->next);
        free(node);
    }
}
void regexx_free(regexx_t *re) {
    _node_free(re->head);
    free(re);
}


/**
 * Add a node to the end of our sequence
 */
node_t *_add_node(regex_t *re)
{
    node_t *result;
    
    result = malloc(sizeof(node_t));
    if (result == NULL) {
        abort();
    }
    memset(result, 0, sizeof(*result));
    
    result->prev = re->tail;
    re->tail->next = result;
    re->tail = result;
    return result;
}


int _next_char(const char *pattern, size_t *offset, size_t length) {
    if (*offset < length) {
        return pattern[(*offset)++];
    }
    return -1;
}
int _peek_char(const char *pattern, const size_t *offset, size_t length) {
    if (*offset < length) {
        return pattern[(*offset)];
    }
    return -1;
}

/**
 * We append a rule onto the end of a list that always matches. It's only through
 * this mechanism that we know that a chain of rules has ended in a proper match.
 * Only a T_TRUE evaluation results in a match of a chain.
 */
void _node_terminate(node_t *node) {
    node_t *terminate;
    
    terminate = malloc(sizeof(node_t));
    memset(terminate, 0, sizeof(node_t));
    terminate->type = T_TRUE;
    terminate->prev = node;

    node->next = terminate;
}

static unsigned char _hexval(int c) {
    if ('0' <= c && c <= '9')
        return c - '0';
    else if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    } else if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    } else
        return 0xFF;
}

static int _unicode_from_number(const char *pattern, size_t *offset, size_t length) {
    unsigned result = 0;
    int c = _next_char(pattern, offset, length);
    if (c == '{') {
        /* We support any number of hex digits until we reach the end `}` */
        for (;;) {
            c = _next_char(pattern, offset, length);
            if (c == '}')
                return result;
            c = _hexval(c);
            if (c > 0xF)
                return ~0;
            result <<= 4;
            result |= c;
        }
    } else if (_hexval(c) <= 0xF) {
        unsigned i;
        result = _hexval(c);
        for (i=0; i<3; i++) {
            c = _next_char(pattern, offset, length);
            c = _hexval(c);
            if (c > 0xF)
                return ~0;
            result <<= 4;
            result |= c;
        }
        return result;
    } else
        return ~0;
}

static bool _name_is_equal(const char *name, const char *pattern, size_t offset, size_t end_offset) {
    size_t length;

    /* For code self-documentation purposes, these are are specified as they
     * appear in regex with the leading and trailing colon */
    if (name[0] == ':')
        name++;
    length = strlen(name);
    if (length && name[length-1] == ':')
        length--;

    /* lengths must be the same */
    if (length != (end_offset - offset))
        return false;

    /* contents must be the same  */
    return memcmp(name, pattern + offset, length) == 0;
}

static int _charclass_add_range(charclass_t *charclass, unsigned first, unsigned last) {
    while (first <= last) {
        _charclass_add_char(charclass, first);
        first++;
    }
    return 0;
}

static int _charclass_from_nameseq(const char *pattern, size_t *offset, size_t length, charclass_t *charclass) {
    charclass_t tmp = {0,0,0,0};
    int c;
    size_t name_offset = *offset;

    /* First, grab the name */
    for (;;) {
        c = _next_char(pattern, offset, length);
        if (!isalpha(c&0xFF))
            break;
    }

    /* Name starts and ends with a colon : */
    if (c != ':')
        return -1;

    /* Grab the character-class */
    if (_name_is_equal(":ascii:", pattern, name_offset, *offset)) {
        /* non-standard extension */
        _charclass_add_range(&tmp, 0x00, 0x7F);
    } else if (_name_is_equal(":alnum:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, 'A', 'Z');
        _charclass_add_range(&tmp, 'a', 'a');
        _charclass_add_range(&tmp, '0', '9');
    } else if (_name_is_equal(":alpha:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, 'A', 'Z');
        _charclass_add_range(&tmp, 'a', 'a');
    } else if (_name_is_equal(":blank:", pattern, name_offset, *offset)) {
        _charclass_add_char(&tmp, ' ');
        _charclass_add_char(&tmp, '\t');
    } else if (_name_is_equal(":cntrl:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, 0x00, 0x1f);
        _charclass_add_range(&tmp, 0x7f, 0x7f);
    } else if (_name_is_equal(":digit:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, '0', '9');
    } else if (_name_is_equal(":graph:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, 0x21, 0x7E);
    } else if (_name_is_equal(":lower:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, 'a', 'a');
    } else if (_name_is_equal(":print:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, 0x20, 0x7e);
    } else if (_name_is_equal(":punct:", pattern, name_offset, *offset)) {
        const char *punct = "[]!\"#$%&'()*+,./:;<=>?@\\^_`{|}~-";
        size_t i;
        for (i=0; punct[i]; i++)
            _charclass_add_char(&tmp, punct[i]);
    } else if (_name_is_equal(":space:", pattern, name_offset, *offset)) {
        const char *space = " \t\r\n\v\f";
        size_t i;
        for (i=0; space[i]; i++)
            _charclass_add_char(&tmp, space[i]);
    } else if (_name_is_equal(":upper:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, 'A', 'Z');
    } else if (_name_is_equal(":word:", pattern, name_offset, *offset)) {
        /* nonstandard */
        _charclass_add_range(&tmp, 'A', 'Z');
        _charclass_add_range(&tmp, 'a', 'z');
        _charclass_add_range(&tmp, '0', '9');
        _charclass_add_char(&tmp, '_');
    } else if (_name_is_equal(":xdigit:", pattern, name_offset, *offset)) {
        _charclass_add_range(&tmp, 'A', 'F');
        _charclass_add_range(&tmp, 'a', 'f');
        _charclass_add_range(&tmp, '0', '9');
    } else {
        /* unknown named set */
        return -1;
    }
    *charclass = tmp;
    return 0;
}

static int _charclass_from_escseq(const char *pattern, size_t *offset, size_t length, charclass_t *charclass) {
    
    charclass_t tmp = {0,0,0,0};
    int c = _next_char(pattern, offset, length);
    switch (c) {
        case -1:
            return -1;
        case 'a':
            _charclass_add_char(&tmp, '\a');
            *charclass = tmp;
            break;
        case 'b':
            _charclass_add_char(&tmp, '\b');
            *charclass = tmp;
            break;
        case 'n':
            _charclass_add_char(&tmp, '\n');
            *charclass = tmp;
            break;
        case 'r':
            _charclass_add_char(&tmp, '\r');
            *charclass = tmp;
            break;
        case 't':
            _charclass_add_char(&tmp, '\t');
            *charclass = tmp;
            break;
        case 'f':
            _charclass_add_char(&tmp, '\f');
            *charclass = tmp;
            break;
        case 'v':
            _charclass_add_char(&tmp, '\v');
            *charclass = tmp;
            break;
        case 'd':
            *charclass = _digits;
            break;
        case 'D':
            *charclass = _invert(_digits);
            break;
        case 'w': /* word */
            *charclass = _word;
            break;
        case 'W':
            *charclass = _invert(_word);
            break;
        case 's':
            *charclass = _whitespace;
            break;
        case 'S':
            *charclass = _invert(_whitespace);
            break;
        case 'c': { /* control character */
            int c2 = _peek_char(pattern, offset, length);
            if ('A' <= c2 && c2 <= 'Z') {
                c2 = _next_char(pattern, offset, length);
                _charclass_add_char(&tmp, c2-'A');
                *charclass = tmp;
            } else
                return -1;
        } break;
        case 'x': {
            unsigned n;
            c = _next_char(pattern, offset, length);
            if (!isxdigit(c&0xFF))
                return -1;
            n = _hexval(c) << 4;
            c = _next_char(pattern, offset, length);
            if (!isxdigit(c&0xFF))
                return -1;
            n |= _hexval(c);
            _charclass_add_char(&tmp, n);
            *charclass = tmp;
        } break;
        case '0':
            if (_hexval(_peek_char(pattern, offset, length)) >= 8) {
                /* if the next character isn't an octal digit, then
                 * this is simply the NUL character. Otherwise,
                 * it's an octal number */
                c = _next_char(pattern, offset, length);
                _charclass_add_char(&tmp, c);
                *charclass = tmp;
                break;
            }
            /* fall through*/
        case '1':
        case '2':
        case '3': {
            /* first octal digit */
            unsigned n = c - '0';

            /* second octal digit */
            c = _next_char(pattern, offset, length);
            if (_hexval(c) >= 8)
                return -1;
            n <<= 3;
            n |= _hexval(c);

            /* third octal digit */
            c = _next_char(pattern, offset, length);
            if (_hexval(c) >= 8)
                return -1;
            n <<= 3;
            n |= _hexval(c);

            _charclass_add_char(&tmp, n);
            *charclass = tmp;
        } break;
        default:
            if (ispunct(c)) {
                _charclass_add_char(&tmp, c);
                *charclass = tmp;
            } else
                return -1;
    }
    return 0;
}

static size_t _is_quantifier(const char *pattern, size_t offset, size_t length) {
    size_t start = offset;
    
    while (isdigit(_peek_char(pattern, &offset, length)&0xFF)) {
        offset++;
    }
    
    if (_peek_char(pattern, &offset, length) == ',')
        offset++;
    
    while (isdigit(_peek_char(pattern, &offset, length)&0xFF)) {
        offset++;
    }
    
    if (_peek_char(pattern, &offset, length) != '}')
        return 0;
    
    return offset - start;
}

static size_t _is_macro(const char *pattern, size_t offset, size_t length) {
    size_t start = offset;
    char c;
    
    /* First char must be /[A-Z_a-z]/ */
    c = _next_char(pattern, &offset, length);
    if (!isalpha(c&0xFF) && c != '_')
        return 0;
    
    /* Next must be alnum /[0-9A-Z_a-z]+/ */
    while (_peek_char(pattern, &offset, length) != '}') {
        c = _next_char(pattern, &offset, length);
        if (!isalnum(c&0xFF) && c != '_')
            return 0;
    }
    
    return offset - start;
}
static size_t _parse_integer(const char *pattern, size_t *offset, size_t length) {
    size_t result = 0;
    while (isdigit(_peek_char(pattern, offset, length)&0xFF)) {
        char c = _next_char(pattern, offset, length);
        result = result * 10 + (c - '0');
    }
    return result;
}

static int _remove_self(regex_t *re, node_t *node) {
    re->tail = node->prev;
    node->prev->next = NULL;
    free(node);
    return 0;
}
/**
 * Add a character. If there's space in the previous string, then append to
 * the end of that one and delete this node. Otherwise, create a new
 * string node.
 */
static int _add_char(regex_t *re, node_t *node, char c) {
    if (node->prev && node->prev->type == T_STRING && node->prev->string.length < sizeof(node->prev->string.chars)) {
        node_t *prev = node->prev;
        
        /* Append to the end of the previous string */
        prev->string.chars[node->prev->string.length++] = c;
        
        /* keep nul-terminated for debugging reasons */
        if (prev->string.length < sizeof(prev->string.chars))
            prev->string.chars[prev->string.length] = '\0';
        return _remove_self(re, node);
    } else {
        node->type = T_STRING;
        node->string.length = 1;
        node->string.chars[0] = c;
        /* keep nul-terminated for debugging reasons */
        node->string.chars[1] = '\0';
    }
    return 0;
}
static int _add_quantifier(regex_t *re, size_t offset, node_t *node, size_t min, size_t max) {

    node->type = T_QUANTIFIER;
    node->quantifier.min = min;
    node->quantifier.max = max;
    
    /* There must be a previous node that this one refers to */
    if (node->prev == re->head || node->prev == NULL) {
        _error_msg(re, "%3u: no previus expression", (unsigned)offset);
        goto fail;
    }
    
    /* The previous node becomes the 'child' of this node */
    node->quantifier.child = node->prev;
    
    /* We become the new 'next' of the previous node, replacing
     * our child */
    node->prev = node->prev->prev;
    node->prev->next = node;
    
    /* The 'child' now starts it's own chain */
    node->quantifier.child->next = NULL;
    node->quantifier.child->prev = NULL;
    
    /* Add a 'terminate' to the child chain */
    _node_terminate(node->quantifier.child);
                    
    return 0;
fail:
    return -1;
}


/**
 * Parses the next expression in a chain.
 *
 * This is the core of what it means to be a **regular** expression, that the language
 * we are parsing goes from left-to-right. We can thus look at the first character
 * of the remainder of the pattern to figure out the next subexpression will be.
 * We don't need to backtrack or anything while parsing the language.
 */
static int _parse_next_node(regexx_t *re, const char *pattern, size_t *r_offset, size_t length) {
    size_t offset = *r_offset;
    int c;
    node_t *node;
    
    /* Get the next character. This will tell us what the following
     * fragment will contain */
    c = _next_char(pattern, &offset, length);
    if (c == -1) {
        _error_msg(re, "%3u: unexpected end of input", (unsigned)offset);
        goto fail;
    }
    
    /* Add the node that will contain this fragment */
    node = _add_node(re);
    
    /* Now depending on the current character, parse the next
     * characters until we've completed this fragment */
    switch (c) {
        case '^':
            node->type = T_ANCHOR_BEGIN;
            break;
        case '$':
            node->type = T_ANCHOR_END;
            break;
        case '.':
            if (re->is_dot_match_newline)
                node->type = T_DOT_ALL;
            else
                node->type = T_DOT_NONEWLINE;
            break;
        case '{':
            if (_is_macro(pattern, offset, length)) {
                macro_t *macro;
                
                _remove_self(re, node);
                
                /* find the macro */
                {
                    size_t name_len = _is_macro(pattern, offset, length);
                    const char *name = pattern + offset;
                    macro = _macro_lookup(re, name, name_len);
                    if (macro == NULL) {
                        _error_msg(re, "%3u: macro not found: {%.*s}", offset, (unsigned)name_len, name);
                        goto fail;
                    }
                    offset += name_len + 1;
                }
                
                /* Parse macro's contents */
                {
                    size_t off = 0;
                    size_t value_len;

                    value_len = strlen(macro->value);
                    while (off < value_len) {
                        int err;
                        err = _parse_next_node(re, macro->value, &off, value_len);
                        if (err != 0) {
                            _error_msg(re,  "%3u: macro error at %u {%s}", offset, off, macro->name, (unsigned)off);
                            goto fail;
                        }
                    }
                }
                break;
            } else if (_is_quantifier(pattern, offset, length)) {
                size_t min = 0;
                size_t max = SIZE_MAX;
                min = _parse_integer(pattern, &offset, length);
                if (_peek_char(pattern, &offset, length) == ',') {
                    offset++;
                    max = _parse_integer(pattern, &offset, length);
                }
                if (_add_quantifier(re, offset, node, min, max) != 0)
                    goto fail;
            } else
                goto fail;
        case '|': {
            node_t *start;
            /* go backwards until the start of the chain */
            for (start=node; start; start = start->prev) {
                if (start->prev == NULL) {
                    _error_msg(re,  "%3u: '|' programming error", offset);
                    goto fail;
                }
                if (start->prev->type == T_ROOT)
                    break;
                if (start->prev->type == T_GROUP_START)
                    break;
            }
            if (start == node) {
                _add_char(re, node, c);
            } else {
                node_t *prev = start->prev;
                
                /* Terminate this chain */
                node->type = T_TRUE;
                
                /* Disconnect the start of the chain */
                start->prev = NULL;
                prev->next = NULL;
                re->tail = prev;
                
                /* Now create a new node for this alternate */
                node = _add_node(re);
                node->type = T_ALTERNATION;
                node->alternation.child = start;
            }
        } break;
        case '(':
            node->type = T_GROUP_START;
            if (_peek_char(pattern, &offset, length) == '?') {
                c = _next_char(pattern, &offset, length);
                switch (_peek_char(pattern, &offset, length)) {
                    case '=': /* (?=ABC) positive lookahead */
                        c = _next_char(pattern, &offset, length);
                        node->group.is_lookahead = true;
                        node->group.is_inverted = false;
                        break;
                    case '!': /* (?!ABC) negative lookahead */
                        c = _next_char(pattern, &offset, length);
                        node->group.is_lookahead = true;
                        node->group.is_inverted = true;
                        break;
                    case '<':
                        _error_msg(re,  "%3u: capture group feature not supported", offset);
                        goto fail;
                    case ':':
                        c = _next_char(pattern, &offset, length);
                        node->group.is_noncapturing = true;
                        break;
                }
            }
            break;
        case ')': {
            node_t *start;
            
            /* hunt backwards until we find the starting group */
            for (start=node->prev; start && start->type != T_GROUP_START; start = start->prev)
                ;
            if (start == NULL) {
                /* there was no group, thus, this is just a normal character
                 * to match on and not a control character */
                _add_char(re, node, c);
            } else {
                /* Change the start to now be the group node */
                start->type = T_GROUP;
                
                /* Move the chain underneath into the child */
                start->group.child = start->next;
                start->next = NULL;
                start->group.child->prev = NULL;
                
                /* Reset 'tail' of the chain to be this group
                 * node instead of the child */
                re->tail = start;
                
                /* Now make this node a terminator */
                node->type = T_TRUE;
            }
        } break;
        case '*':
            if (_add_quantifier(re, offset, node, 0, SIZE_MAX) != 0)
                goto fail;
            break;
        case '+':
            if (_add_quantifier(re, offset, node, 1, SIZE_MAX) != 0)
                goto fail;
            break;
        case '?':
            if (node->prev && node->prev->type == T_QUANTIFIER) {
                node->prev->quantifier.is_lazy = true;
                node->prev->next = NULL;
                re->tail = node->prev;
                free(node);
            } else {
                if (_add_quantifier(re, offset, node, 0, 1) != 0)
                    goto fail;
            }
            break;
        
        /* Escaped character-classes (\s \w ...): */
        case '\\':
            if (_peek_char(pattern, &offset, length) == 'u') {
                unsigned uc;
                
                /* unicode character */
                c = _peek_char(pattern, &offset, length);
                uc = _unicode_from_number(pattern, &offset, length);
                if (uc == ~0)
                    return -1;
                if (uc < 0x00007F) {
                    _add_char(re, node, uc);
                } else if (uc < 0x0007FF) {
                    _add_char(re, node, (uc>>6) | 0xc0);
                    _add_char(re, node, ((uc>>0) & 0x3F) | 0x80);
                } else if (uc < 0x00FFFF) {
                    _add_char(re, node, (uc>>12) | 0xE0);
                    _add_char(re, node, ((uc>>6) & 0x3F) | 0x80);
                    _add_char(re, node, ((uc>>0) & 0x3F) | 0x80);
                } else if (uc < 0x10FFFF) {
                    _add_char(re, node, (uc >> 18) | 0xF0);
                    _add_char(re, node, ((uc >>12) & 0x3F) | 0x80);
                    _add_char(re, node, ((uc >> 6) & 0x3F) | 0x80);
                    _add_char(re, node, ((uc >> 0) & 0x3F) | 0x80);
                } else {
                    return -1;
                }
            } else {
                int err;
                charclass_t charclass;
                
                err = _charclass_from_escseq(pattern, &offset, length, &charclass);
                if (err == -1) {
                    _error_msg(re, "%3u: bad escape sequence", (unsigned)offset);
                    goto fail;
                }
                if (_charclass_count(charclass) == 1) {
                    _add_char(re, node, _charclass_first_char(charclass));
                } else {
                    node->type = T_CHARCLASS;
                    node->charclass = charclass;
                }
            }
            break;

        /* Character class: */
        case '[': {
            bool is_inverted = false;
            charclass_t charclass = {0,0,0,0};
            int prev = -1;
            
            node->type = T_CHARCLASS;
            
            c = _next_char(pattern, &offset, length);
            if (c == '^') {
                is_inverted = true;
                c = _next_char(pattern, &offset, length);
            }
    
            
            while (c != ']') {
                if (c == -1) {
                    /* must terminate with ']' char */
                    goto fail;
                } else if (c == '\\') {
                    int err;
                    charclass_t e;
                    
                    err = _charclass_from_escseq(pattern, &offset, length, &e);
                    if (err == -1) {
                        _error_msg(re, "%3u: bad charact class escape sequence", (unsigned)offset);
                        goto fail;
                    }
                    
                    charclass = _charclass_merge(charclass, e);
                    prev = -1;
                } else if (c == '-' && (prev == -1 || _peek_char(pattern, &offset, length) == ']')) {
                    /* this cannot be a range, so use raw character
                     * instead of control */
                   _charclass_add_char(&charclass, '-');
                   prev = c;
                } else if (c == '-') {
                    
                    if (prev == -1)
                        goto fail;
                    c = _next_char(pattern, &offset, length);
                    if (c == -1) {
                        _error_msg(re, "%3u: unexpected end of input", (unsigned)offset);
                        goto fail;
                    }
                    if (c == ']') {
                        /* */
                    }
                    if (c == '\\') {
                        c = _next_char(pattern, &offset, length);
                        if (c == -1) {
                            _error_msg(re, "%3u: unexpected end of input", (unsigned)offset);
                            goto fail;
                        }
                    }

                    _charclass_add_range(&charclass, prev, c);
                    prev = -1;
                } else if (c == '[' && _peek_char(pattern, &offset, length) == ':') {
                    int err;
                    charclass_t e;
                    char c = _peek_char(pattern, &offset, length); /* consume ':' */

                    err = _charclass_from_nameseq(pattern, &offset, length, &e);
                    if (err == -1) {
                        _error_msg(re, "%3u: bad character class name", (unsigned)offset);
                        goto fail;
                    }
                    c = _next_char(pattern, &offset, length); /* consume ']' */
                    if (c != ']')
                        return -1;

                    charclass = _charclass_merge(charclass, e);
                    prev = -1;
                } else {
                    prev = c;
                    _charclass_add_char(&charclass, c);
                }


                node->charclass = charclass;

                c = _next_char(pattern, &offset, length);
            }
            
            if (is_inverted)
                charclass = _invert(charclass);
            node->charclass = charclass;
        } break;

        default:
            /* Anything that's not a control character is a valid matching
             * character. This is the normal case that will compromise much
             * of the regular expression */
            _add_char(re, node, c);
            break;
    }
    *r_offset = offset;
    return 0;
fail:
    return -1;
}

int regexx_add_pattern(regexx_t *re, const char *pattern, size_t id, unsigned flags) {

    size_t length;
    size_t offset = 0;

    if (re == NULL)
        return -1;
    
    length = pattern?strlen(pattern):0;
    
    /*
     * Parse the chain of subexpressions left to right
     */
    while (offset < length) {
        int err;
        
        err = _parse_next_node(re, pattern, &offset, length);
        if (err != 0)
            goto fail;
    }

    _node_terminate(re->tail);
    
    /* Append to our list of patterns */
    re->patterns = realloc(re->patterns, sizeof(re->patterns[0]) * (re->pattern_count+1));
    re->patterns[re->pattern_count].head = re->head;
    re->patterns[re->pattern_count].id = id;
    re->pattern_count++;
    
    /* Add a new head */
    re->head = malloc(sizeof(node_t));
    memset(re->head, 0, sizeof(node_t));
    re->head->type = T_ROOT;
    re->tail = re->head;
    return 0;
fail:
    return -1;
}





static bool _node_eval(node_t *node, const char *text, size_t offset, size_t length, size_t *next_offset) {
    size_t offset2 = offset;
    size_t count;
    size_t longest;
    
    if (offset >= length && (node->type != T_QUANTIFIER || node->quantifier.min != 0)) {
        if (node->type != T_TRUE && node->type != T_ANCHOR_END)
            return false;
    }
    
 
    
    switch (node->type) {
        case T_TRUE:
            *next_offset = offset;
            return true;
        case T_ROOT:
            return _node_eval(node->next, text, offset, length, next_offset);
        case T_ANCHOR_BEGIN:
            if (offset != 0)
                return false;
            return _node_eval(node->next, text, offset, length, next_offset);
        case T_ANCHOR_END:   /* '$' at end of regex */
            if (offset != length)
                return false;
            return _node_eval(node->next, text, offset, length, next_offset);
        case T_ALTERNATION:
            if (_node_eval(node->alternation.child, text, offset, length, &offset2)) {
                size_t offset3;
                if (_node_eval(node->next, text, offset, length, &offset3)) {
                    if (offset2 > offset3) {
                        *next_offset = offset2;
                        return true;
                    } else {
                        *next_offset = offset3;
                        return true;
                    }
                } else {
                    *next_offset = offset2;
                    return true;
                }
            } else
                return _node_eval(node->next, text, offset, length, next_offset);
        case T_GROUP:
            /* Match group.
             * Also handle "lookaround" */
            if (_node_eval(node->group.child, text, offset, length, &offset2)) {
                if (node->group.is_inverted)
                    return false;
                if (node->group.is_lookahead)
                    offset2 = offset; /* remove what was matched */
                return _node_eval(node->next, text, offset2, length, next_offset);
            } else {
                if (!node->group.is_inverted)
                    return false;
                if (node->group.is_lookahead)
                    offset2 = offset; /* remove what was matched */
                return _node_eval(node->next, text, offset2, length, next_offset);
            }
        case T_QUANTIFIER:
            longest = 0;
            
            /* Do the minimum number of steps
             * `offset` will be set to the next character after a successful match */
            for (count=0; count==SIZE_MAX || count<node->quantifier.min; count++) {
                bool x;
                x = _node_eval(node->quantifier.child, text, offset, length, &offset);
                if (!x)
                    return false;
            }
            
            /* if lazy and rest of chain matches, then stop right here 
             * `longest` will be set to the last character of a successful match,
             * which will be used below in case no other matches are found */
            if (_node_eval(node->next, text, offset, length, &longest)) {
                if (node->quantifier.is_lazy) {
                    *next_offset = longest;
                    return true;
                }
            }

            /* Do up to the maximum number of steps */
            for (; count==SIZE_MAX || count<node->quantifier.max; count++) {
                bool x;


                x = _node_eval(node->quantifier.child, text, offset, length, &offset2);
                if (!x)
                    break;
                
                x = _node_eval(node->next, text, offset2, length, &longest);
                if (x && node->quantifier.is_lazy)
                    break;
                offset = offset2;
            }
            
            if (longest) {
                *next_offset = longest;
                return true;
            }
            return false;
            

        case T_STRING:
            if (node->string.length > (length-offset)) {
                /* Pattern longer than remaining characters */
                return false;
            }
            if (node->string.is_case_insensitive) {
                /* FIXME: make this case insensitive */
                if (memcmp(text+offset, node->string.chars, node->string.length) != 0) {
                    /* the characters didn't match */
                    return false;
                }
            } else {
                if (memcmp(text+offset, node->string.chars, node->string.length) != 0) {
                    /* the characters didn't match */
                    return false;
                }
            }
            return _node_eval(node->next, text, offset+node->string.length, length, next_offset);
        case T_DOT_ALL:
            return _node_eval(node->next, text, offset+1, length, next_offset);
        case T_DOT_NONEWLINE:
            if (text[offset] == '\n' || text[offset] == '\r')
                return false;
            return _node_eval(node->next, text, offset+1, length, next_offset);
        case T_CHARCLASS:
            if (!_charclass_match_char(&node->charclass, text[offset]))
                return false;
            return _node_eval(node->next, text, offset+1, length, next_offset);
        default:
            fprintf(stderr, "[-] programming err\n");
            abort();
    }
    return 0;
}

/**
 * Keep track of line numbers, and the character offset in the current line, for each
 * token.
 * TODO: this needs to be integrated into `lex` parsing instead of a separate step
 */
static void _set_offsets(regexx_t *re, const char *buf, size_t offset, size_t token_length) {
    size_t i;
    
    for (i=0; i<token_length; i++) {
        if (buf[offset + i] == '\n') {
            re->offsets.line_number++;
            re->offsets.char_number = 0;
        } else
            re->offsets.char_number++;
    }
}

struct regexxtoken_t regexx_lex_token(regexx_t *re, const char *subject, size_t *subject_offset, size_t subject_length) {
    struct regexxtoken_t result = {REGEXX_NOT_FOUND, 0, 0 , 0, 0};
    size_t i;
    size_t longest = 0;
    
    result.line_number = re->offsets.line_number;
    result.char_number = re->offsets.char_number;
    
    /* Make sure input is valid */
    if (re == NULL || re->head == NULL || subject == NULL)
        return result;

    if (subject_length == SIZE_MAX)
        subject_length = strlen(subject);
    
    /* Search for all patterns that have been compile */
    for (i=0; i<re->pattern_count; i++) {
        node_t *head = re->patterns[i].head;
        bool is_matched;
        size_t end;
        
        is_matched = _node_eval(head->next, subject, *subject_offset, subject_length, &end);
        if (is_matched && longest < end) {
            result.id = re->patterns[i].id;
            result.length = end - *subject_offset;
            _set_offsets(re, subject, *subject_offset, result.length);
            result.string = subject + *subject_offset;
            longest = end;
        }
    }
    
    /* If there was a match, return the longest */
    if (longest) {
        *subject_offset = longest;
        return result;
    } else {
        result.id = REGEXX_NOT_FOUND;
        return result;
    }
}

size_t regexx_match(regexx_t *re, const char *input, size_t in_offset, size_t in_length, size_t *out_offset, size_t *out_length) {
    size_t i;

    if (in_length == SIZE_MAX)
        in_length = strlen(input);
    
    /* Make sure input is valid */
    if (re == NULL || re->head == NULL || input == NULL)
        return -1;
    
    /* Search for all patterns that have been compile */
    for (i=0; i<re->pattern_count; i++) {
        size_t offset;
        node_t *head = re->patterns[i].head;
        size_t id = re->patterns[i].id;
        
        /* For all bytes, see if there is a match at this location,
         * until we find one, then stop. */
        for (offset=in_offset; offset<in_length; offset++) {
            bool is_matched;
            size_t end;
        
            is_matched = _node_eval(head->next, input, offset, in_length, &end);
            if (is_matched) {
                *out_offset = offset;
                *out_length = end - offset;
                return id; 
            }
        }
    }
    return REGEXX_NOT_FOUND;
}


static void _node_print_chars(const char *str, size_t length, buf_t *buf) {
    size_t i;
    for (i=0; i<length; i++) {
        char c = str[i];
        if (strchr(".^$*+?()[{}\\|", c)) {
            _appendf(buf, "\\%c", c);
        } else {
            switch (c & 0xFF) {
            case '\a': _appendf(buf, "\\a"); break;
            case '\b': _appendf(buf, "\\b"); break;
            case '\t': _appendf(buf, "\\t"); break;
            case '\f': _appendf(buf, "\\f"); break;
            case '\v': _appendf(buf, "\\v"); break;
            case '\r': _appendf(buf, "\\r"); break;
            case '\n': _appendf(buf, "\\n"); break;
            default:
                _appendf(buf, "%c", c);
                break;
            }
        }
    }
}


/**
 * Recursively print the nodes
 */
static const char *_node_print(node_t *node, buf_t *buf) {
    buf_t buf2[1] = {0,0};
    if (buf == NULL)
        buf = buf2; /* for debugging */

    while (node) {
    switch (node->type) {
        case T_TRUE:
            return buf->string;
            break;
        case T_ROOT:
            break;
        case T_DOT_ALL:
        case T_DOT_NONEWLINE:
            _appendf(buf, ".");
            break;
        case T_ANCHOR_BEGIN:
            _appendf(buf, "^");
            break;
        case T_ANCHOR_END:
            _appendf(buf, "$");
            break;
        case T_QUANTIFIER:
            if (node->quantifier.min == 0 && node->quantifier.max == 1) {
                _node_print(node->quantifier.child, buf);
                _appendf(buf, "?");
            } else if (node->quantifier.min == 0 && node->quantifier.max == SIZE_MAX) {
                _node_print(node->quantifier.child, buf);
                _appendf(buf, "*%s", node->quantifier.is_lazy?"?":"");
            } else if (node->quantifier.min == 1 && node->quantifier.max == SIZE_MAX) {
                _node_print(node->quantifier.child, buf);
                _appendf(buf, "+%s", node->quantifier.is_lazy?"?":"");
            } else if (node->quantifier.min == 1 && node->quantifier.max == 1) {
                _node_print(node->quantifier.child, buf);
            } else if (node->quantifier.min == 0 && node->quantifier.max == 0) {
                ;
            } else {
                _node_print(node->quantifier.child, buf);
                if (node->quantifier.min == node->quantifier.max) {
                    _appendf(buf, "{%u}%s", (unsigned)node->quantifier.max, node->quantifier.is_lazy?"?":"");
                } else if (node->quantifier.min == 0) {
                    _appendf(buf, "{,%u}%s", (unsigned)node->quantifier.max, node->quantifier.is_lazy?"?":"");
                } else if (SIZE_MAX == node->quantifier.max) {
                    _appendf(buf, "{%u,}%s", (unsigned)node->quantifier.min, node->quantifier.is_lazy?"?":"");
                } else {
                    _appendf(buf, "{%u,%u}%s", (unsigned)node->quantifier.min, (unsigned)node->quantifier.max, node->quantifier.is_lazy?"?":"");
                }
            }
            break;
        case T_ALTERNATION:
            _node_print(node->alternation.child, buf);
            _appendf(buf, "|");
            break;
        case T_GROUP:
            _appendf(buf, "(");
            _node_print(node->group.child, buf);
            _appendf(buf, ")");
            break;
        case T_STRING:
            _node_print_chars(node->string.chars, node->string.length, buf);
            break;
        case T_CHARCLASS:
            if (_charclass_is_equal(node->charclass, _whitespace))
                _appendf(buf, "\\s");
            else if (_charclass_is_equal(node->charclass, _invert(_whitespace)))
                _appendf(buf, "\\S");
            else if (_charclass_is_equal(node->charclass, _word))
                _appendf(buf, "\\w");
            else if (_charclass_is_equal(node->charclass, _invert(_word)))
                _appendf(buf, "\\W");
            else if (_charclass_is_equal(node->charclass, _digits))
                _appendf(buf, "\\d");
            else if (_charclass_is_equal(node->charclass, _invert(_digits)))
                _appendf(buf, "\\D");
            else if (_charclass_is_equal(node->charclass, _dot_all))
                _appendf(buf, "[\\s\\S]");
            else {
                char normal[1024];
                char inverted[1024];
                _charclass_print(node->charclass, normal, sizeof(normal));
                _charclass_print(_invert(node->charclass), inverted, sizeof(inverted));
                if (strlen(normal) < strlen(inverted)) {
                    _appendf(buf, "[%s]", normal);
                } else {
                    _appendf(buf, "[^%s]", inverted);
                }
            }
            break;
        case T_GROUP_START:
            /* Only useful when printing partial regex chains, such as while debugging.
             * If seen in actual output, this is an error */
            _appendf(buf, "(");
            break;
        default:
            _appendf(buf, "*****errror*****");
            return buf->string;
    }
        node = node->next;
    }
    return buf->string;
}

char *regexx_print(regexx_t *re, size_t index, size_t *id, bool is_flag_shown)
{
    node_t *head;
    buf_t buf[1] = {0,0};
 
    if (index >= re->pattern_count) {
        if (id)
            *id = REGEXX_NOT_FOUND;
        return NULL;
    } else {
        head = re->patterns[index].head;
        if (id)
            *id = re->patterns[index].id;
    }
    
    //_appendf(buf, "");
    
    if (is_flag_shown)
        _appendf(buf, "/");
    _node_print(head, buf);
    if (is_flag_shown)
        _appendf(buf, "/");
    return buf->string;
}

regexx_t *regexx_create(unsigned flags) {
    regexx_t *re;
    
    re = malloc(sizeof(*re));
    memset(re, 0, sizeof(*re));
    
    re->head = malloc(sizeof(node_t));
    memset(re->head, 0, sizeof(node_t));
    re->head->type = T_ROOT;
    re->tail = re->head;
    re->offsets.line_number = 1;
    re->is_dot_match_newline = 1;
    return re;
}

const char *regexx_get_error_msg(regexx_t *re)
{
    if (re == NULL) {
        return "[-] Regexx object doesn't exist";
    }
    return re->error_msg.string;
}
