# C Grammar

## Translation Phases

1. Character mapping
     Convert source-file to UTF-8, and replace \r\n with \n.
     If trigraphs supported, they are handled at this time.
     
2. Line splicing
     Lines ending in back slash \ are spliced back together.
     A final new-line  \n is added if one doesn't already exist.
     
3. Tokenization
    File is converted to tokens.
    Comments are converted to a space.
    
4. Preprocessing
    Preprocessing tokens are executed and macros replaced.

5. Character-set mapping
    Ignored, assumed to be UTF-8.

6. Translation
   Grammar parsed and converted to Abstract Syntax Tree (AST)

