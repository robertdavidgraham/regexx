# regexx
My unfinished regex library for multi-pattern matching.

## What is this project

This is like a regular-expression library, except it's built for
matching multiple patterns. It's intended use is for things like
lexical analysis (`lex` or `flex`), intrusion-detection (like `snort`),
deteting IoCs, and other uses.

When a regular-expression matches, you recieve the `id` of which pattern
matched, as well as the text of the match.

## Replacing `lex`

Among the examples is a program `examples/clex.c` that shows using this
library as a lexical analyzer for parsing C. It's based on the `lex` file
for C11 at (http://www.quut.com/c/ANSI-C-grammar-l-2011.html).

It produces essentiall the same result, but is written as a C program instead
of a `lex` file.

## What needs to be finished

It's currently an NFA, effectively (though not quite). I need to convert it
to a DFA. The speed difference is enormous.



