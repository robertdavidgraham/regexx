# Regular-expressions specification

 [ABC] - character class
 [^ABC] - negated set
 [A-Z] - range
 [\s\S] - any character
 . - any character (depending on flags, equivelent to [^\n\r]).
 \w - word, equivelent to [A-Za-z0-9_]
 \W - not word, equivelent to [^A-Za-z0-9_]
 \d - digit, equivelent to [0-9]
 \D - not digit, equivelent to [^0-9]
 \s - whitespace
 \S - not whitespace
 \+ - escaped characters, +*?^$\.[]{}()|/ need escapes, but within char classes, only \-] need escapes
\000 - octal escape
\xFF - hex escape
\xuFFFF - unicode escape (maps to UTF-8 sequence)
\cI - control character escape (1 to 26)
\t - tab
\n - newline
\v - verticale tab
\f - form feed
\r - carriage return
\0 - null
(ABC) - group
* - kleen star, equivelent to {0,}
+ - plus, equevelent to {1,}
? - optional, equivelent to {,1}
{}? - lazy (non-greedy) match
| - alternation (or)

 
