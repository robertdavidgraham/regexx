# regexx
My unfinished regex library for multi-pattern matching.

## What is this project

This is like a regular-expression library, except it's built for
matching multiple patterns. It's intended use is for things like
lexical analysis (`lex` or `flex`), intrusion-detection (like `snort`),
detecting IoCs, and other uses.

When a regular-expression matches, you recieve the `id` of which pattern
matched, as well as the text of the match (and, in the future, captures).

## Replacing `lex`

Among the examples is a program [`examples/c-lex.c`](examples/c-lex.c) that shows using this
library as a lexical analyzer for parsing C. It's based on the `lex` file
for C11 at (http://www.quut.com/c/ANSI-C-grammar-l-2011.html). Thus, you see
unusual features that work more like regexp in `lex` than PERL-compatible regex.

For example, it supports "macros". These aren't really needed when 
using a single expression, but are useful when building lots
of expressions. They also make regexps much more readable. Thus,
I can define a macro `{hex}` that can be used in place of `[0-9A-Fa-f]`.
A hexadecimal integer constant would then be `0[xX]{hex}+`. This is how
`lex` works.

On the other hand, there appear features of standard regexp that `lex` appears
not to use. One feature is how it it deals with comments, controlling the
"greedy" vs. "lazy" feature. In other words, the following doesn't match
a C comment, because it matches from the first `/*` to the last `*/` in a file.

    [/][*].*[*][/]
   
In modern regexp, this can be controlled by putting a `?` after the `*` kleen
star, making it *lazy* instead of *greedy*:

    [/][*].*?[*][/]

Another example is the `//` C++ style comment. It needs to match until the 
end-of-line, but not include the end-of-line, which needs to be a separate
token. This can be solved with the rarely used *lookaround* feature of regexp,
encoded as `(?=xxxx)`.

    //.*?(?=\n)

Thus, as you see in the [`examples/c-lex.c`](examples/c-lex.c) file, I can do
the job of `lex` with just standard regexp. All I need is a library that allows
many regexp to be specified and return which matched. I put all the *tokens* in
an array and register them all.

First I have a table of macros to register at the beginning:

    {"D",   "[0-9]"},
    {"HP",  "(0[xX])"},
    {"E",   "([Ee][+-]?{D}+)"},
    {"P",   "([Pp][+-]?{D}+)"},
    {"FS",  "(f|F|l|L)"},
    {"IS",  "(((u|U)(l|L|ll|LL)?)|((l|L|ll|LL)(u|U)?))"},
    {"CP",  "(u|U|L)"},
 
Then I have a table of the regular expressions, and which token they return:

    {T_KEYWORD, "enum"},
    {T_OP, "=="},
    {T_OP, "!="},
    {T_OP, "\\+\\+"},
    {T_FLOAT, "{D}+{E}{FS}?"},
    {T_FLOAT, "{D}*\\.{D}+{E}?{FS}?"},
    {T_STRING, "({SP}?\\\"([^\"\\n]|{ES})*\\\"{WS}*)+"},
    {T_INTEGER, "0{O}*{IS}?"},             /* octal integer */
    {T_INTEGER, "{CP}?'([^'\\\\n]|{ES})+'"},   /* character constant */
    {T_COMMENT, "\\/\\*.*?\\*\\/"}, /* comment */
    {T_COMMENT, "\\/\\/.*?(?=\\n)"}, // comment
    
Tokenizing is just a matter of looping through the input, grabbing tokens 
one-by-one.

## What needs to be finished

### DFA vs. backtracking

DFA DFA DFA DFA.

The purpose of this library is to match **multiple patterns** at once. This
ultimately means a *mostly DFA* architecture that reverts to some other
processing for some states.

The currently implementation is nowhere close to this.

Instead, it does **naive backtracking**. In fact, each pattern is still 
independent from the others. Underneeth, it simply loops over all patterns
one-by-one and returns the longest match. This is pointless -- because any
regexp library can be used in such a mode.

I need a compilation step that will integrate these all into what will
mostly be a DFA on the front-end.

Once I make this change, this library will be in a "finished" state. It still doesn't
support all POSIX or PERL compatible regexp, but it's close enough to be useful.

## Engine comparision

According to these [comparisons](https://en.wikipedia.org/wiki/Comparison_of_regular-expression_engines)
the engine supports these features:

  - yes:`+` quantifier
  - yes: `[^ABC]` negated character classes
  - yes: `A*?` non-greedy quantifiers
  - yes: `(?:ABC)` shy/non-capturing groups
  - yes: recursion (note: there's no stack checks, so can crash)
  - yes: `(?=ABC)` look-ahead
  - no: `(?<=ABC)` look-behind
  - no: `\1` back-references
  - 0: number of indexable captures
  - no: `(?i:test)` directives
  - no: `(?(?=ABC)one|two))` conditionals
  - no: `(?>bc|b)` atomic groups
  - no: `(?P<name>ABC)(?P=name)` named captures
  - no: `(?#comment)` comments
  - no: embedded code
  - no: `\p{Script=Greek}[:Script=Greek:]` Unicode properties
  - no: balancing groups
  - no: variable look-behinds
  - no: UTF-16
  - partial: UTF-8
  - no: multiline matching
  - no: partial matching


## Unsupported regular expressions clauses

Currently (as if this writing) the code supports most all features. It's easier
listing the features it doesn't support.

It doesn't support **flags** yet, either as an extra formatting `/ABC/igm` or
in the API call. There is an enum for these flags that you can pass along
with a pattern, but they are ignored. Thus, you can't do:

    /ABC/igmuys

The **locale** is always UTF-8. It doesn't support locale-specific features
like **collating sequences** or **character equivelents**.

    [.ch.]
    [[=x=][=z=]]


As for **anchors**, they only match at the beginning and end of the entire text,
without *line-mode* support (see flags). Thus, you can use `^` and `$`, just
not for lines. You want to do the following, but you can't:

    /^AB.*C$/m

As for **groups**, they are all non-capturing at the moment (but I want to in
the future). Thus, `(ABC)` and `(:ABC)` are treated the same. It'll correctly
parse the "capturing" flag, but ignore it. Needless to say, you can't do the 
following:

    (?<name>ABC)
    \1

As for **lookaround**, it supports it in the **forwards** direction but
not **backwards**. Thus, while you can do `(?=ABC)`, you can't do:

    (?<=ABC)
    (?<!ABC)

As for **Unicode**, it doesn't support anything other than parsing `\uFFFF`
and `\u{FFF}` into a sequence of UTF-8 characters (as if you specified
them manually like `\xC1\x89`. It doesn't support *Unicode character-classes*
or a lot of other features:

    [:^script=greek:]
    [\u{3040}-\u{309F} \u{30FC}]
    \u{63 64}
    \p{Whitespace}
    \p[Ll]
    \P[Ll]

It doesn't support **word boundaries** yet or **line breaks**:

    \b
    \B
    \R

The **substitutions** feature is not supported, since this is focused
on pattern-matching and not general use, it might never be supported.

    $&
    $1
    $`
    $'
    $$



