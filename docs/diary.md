# diary - writing my own static-analyzer for C

## Create regexp library (`regexx.c`)

I want to write a C language parser, but I don't want to work with
`lex` files. I just want to use a regular-expression library to do the lexing.
This means being able to match multiple patterns.

I searched and searched and didn't find anything that I like. PCRE2 using
multiple capture groups might work, but it's not really oriented to the task.
I spent some time with Google's `Re2` library, as it's got some ability for 
multiple match, but it's still not all the way there. It looks like I could change
the library to add what I want, but at this point, it looks easier just to write
my own library.

This is turning out to be far easier than I expected. Every struggles to
read regular-expression as they are some sort of cryptic magic that they
never quite learn.

However, *parsing* writing your own regular-expression library is quite
simple -- easier than writing regular-expressions themselves. That's what
the name 'regular-expression' means: an expression that's regular and hence
easy to parse and evaluate.

In other words, as you parse the expression, you look at the next character
like '(' or '+' and you know what sort of clause is coming next. There's none
of the difficulty you have in parsing programming languages like C.

## Done with `regexx.c`

I'm pretty much done with `regexx.c` after the weekend working on it.

Evaluation is pretty messy. For multiple pattern match, I simply loop through
and match all the patterns one-by-one. This is stupid: I could've used any
regexp library is that was going to be the way I was going to do it.

The intent is that I'll eventually convert the current NFA-like structure into
a DFA, at which point, I can match all the patterns in one step.

Unfortunately, I'm at the limits of my theoretical computer science knowledge.
I know that NFA's are easily convertable into DFAs. And I know how I want
the resulting DFA to look like (namely, like my `smack.c` Aho-Corasick library
that compiles directly into a DFA).

## Create the lexer (`c-lex.c`)

Now that I have a regexp library, it's time to create my lexer.

All this does is register all the regexp patterns that represent the tokens
it's going to match, but as function calls in C rather than as a separate file
as in traditional `lex`.

This URL contains a nice lex file for C11. 

[c.l](http://www.quut.com/c/ANSI-C-grammar-l-2011.html)

However, that file is for parsing things after they've been preprocessed.
Dealing with preprocessor makes things a lot more complicated.


## `regexx.c`: add macros

The sorts of regexp in `lex` files is a bit different than standard. It supports some sort
of macro system. This looks pretty trivial to add to my library. For example, is define's
it's own hex digit macro:

    H   [a-fA-F0-9]
    IS  (((u|U)(l|L|ll|LL)?)|((l|L|ll|LL)(u|U)?))
    
It then uses that when defining a hex integer constant:
    
    {HP}{H}+{IS}?       { return I_CONSTANT; }

Existing regexp libraries don't support such a feature because they match only
a single regexp at a time, so it's kinda pointless. It's only useful when you need
to specify many regular-expressions.

## `regexx.c`: add laziness to kleen star

The regexp library is greedy by default. That means the following expression
for C comments won't work:

    /[*].*[*]/

That's because greediness will make it go past the end of the first comment
all the way to the end of the last comment in the entire file.

In the example `lex` files, this is handled by simply writing a separate function
in C to handle this special case. I could do that. In much the same way I added
the macro system, I could instead register a function to process things.

However, modern regexp syntax that has evolved since 1980 (when `lex` was
written) seems to have a solution: a laziness operator.

The standard regexp syntax is apparently adding a question-mark ? after the 
kleen star.

    /[*].*?[*]/
    
Apparently, I already added this to my regexp library. I noticed the specification
while writing the code and added it, without test cases, and then promptly forgot
about it.

So I didn't have to add anything. I change the `c-lex.c` pattern, and now it works.


## `regexx.c`: add lookahead

I also need to support the C++ comment method. The problem is that I don't want
to include the '\n' newline at the end of the token. I want to parse that as a separate
newline token.

One solution is just a special case in the code, that when I see this sort of token,
add a separate T_NEWLINE token, and shorten the comment token.

Apparently, there's regexp syntax that can also handle this, the "lookaround" feature. 
This matches on text, but then doesn't include those characters in the match.
Another way of describing this is that of a zero-length match. Thus, I can match
on the newline, but not include it in my token.

Thus, the regexp for a C++ comment looks like:

    //.*?(?=\n)

This was a trivial change. I get the feeling that this is how they kept extending
regexp over the past decades, adding one more than in the way that could be
done with the minimum amount of code. As I extend my own library to match
the new feature, I likewise have to add only a few lines of code to implement
what on the outside looks like a hugely complex feature. It takes more lines
of confusing English text tryingto explain what the "lookaround" feature does
than implementing it in my code. There's also a "lookbehind" feature, but I'm
not sure how to implement that, though.


## Line splicing

Lines can end with a slash \ meaning the compiler needs to splice
them back to gether. This is considered the second pass of the
compiler. It changes patterns like this to a single space:

    \\[\r]*[\n]

I could quickly do this in a pass over the file. However, this would
mess up line-numbers. One solution is to just make an array 
of all lines in the file and their associated line-numbers. This 
seems like a ration thing to do since lexing/tokenization in
the preprocessor seems line-oriented anyway.

Or, I could just lex out the splices. I've written some patterns
to do this and it seems to work. The first step is adding a macro
for the splice, to avoid the regexp's getting too hairy:

    SPLICE  \\[\r]*[\n]
    WS2     [ \t\v\f\r]

I need to write an addition whitespace regexp that matches the splice.
The following seems to work. It'll catch multiple spliced lines.

    {WS2}*({SPLICE}+{WS2}*)+

However, the C++ style comment // ends with an end-of-line, but
after splicing. Thus, that also needs to to have another definition
for splices in the middle. I tried multiple versions, but the lazy
matching really messes it up. I got he following to work.

    //([^\n]*?{SPLICE})+[^\n]*?(?=\\n)

Again, this will work when multiple such comments are spliced
together.

I should really spit out a warning whenever such things are seen,
because it's an obfuscated programming trick rather than a
legitimate way to write comments.


## Create the preprocessor (`c-preproc.c`)

Reference: [C11 proposal](http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1548.pdf)

Now that I've fully lexed all the tokens in a file, it's time to write the 
C preprocessor.

There are the following *translation phases* or *passes* of the compiler.

1. character set translation
I don't do any of this. I assume the user has run `sed` to convert
trigraphs (which are deprecated anyway), `icov` to convert to
UTF-8 (if necessary). In include carriage return in the lexer regexp
so that this is handled later.

2. line splicing
I don't do this. As I describe above, I think I can handle this with
the lexer, which will make tracking original line numbers easier
for error messages

3. tokenization
This phase converts the text to an array of tokens, using the
lexer. I'm struggling to figure out how this works when trying
to parse `#include <stdio.h>`, because otherwise that
looks like less-than `<` and greater-than `>` tokens. It's a
context sensitive lexical analysis. I'm currently solving
this by an ugly hack

4. proprocessing
At this phase, I walk through the tokens and execute all the
preprocessing directives. This includes processing more
files (going through these phases again) when processing
an `#include` directive.

5. character-set conversion
Another character-set conversion step which I skip because
I'm assuming UTF-8 for everything.

6. string-literal concatenation
This seems straightforward, though it's something I might be able to
do earlier.

7. grammar
It's at this phase that I convert the tokens into an abstract-syntax tree.

8. linking
My static analyzer doesn't have this stage, though technically, it'll
run a separate analysis step that looks across many source files.



