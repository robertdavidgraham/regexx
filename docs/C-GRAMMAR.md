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

References:
 * (https://en.wikipedia.org/wiki/C_preprocessor)
 * (https://news.ycombinator.com/item?id=14445015)
 * (https://blog.robertelder.org/building-a-c-compiler-type-system-the-formidable-declarator/)
 

## Preprocessor grammar

**control-line:**  
   `#`  
 `#define` identifier token-stringᵒᵖᵗ  
 `#define` identifier `(` identifierᵒᵖᵗ , ... , identifierᵒᵖᵗ `)` token-stringᵒᵖᵗ  
 `#include` " filename "  
 `#include` < filename >  
 `#line` digit-sequence "filename"ᵒᵖᵗ  
 `#undef` identifier  
 `#error` token-string  
 `#warning` token-string  
 `#pragma` token-string  

**constant-expression:**  
 `defined` ( identifier )  
 `defined` identifier  
 any other constant expression  

**conditional:**  
 if-part elif-partsᵒᵖᵗ else-partᵒᵖᵗ endif-line  

**if-part:**  
 if-line text  

**if-line:**  
 `#if` constant-expression  
 `#ifdef` identifier  
 `#ifndef` identifier  

**elif-parts:**  
 elif-line text  
 elif-parts elif-line text  

**elif-line:**  
 `#elif` constant-expression  

**else-part:**  
 else-line text  

**else-line:**  
 `#else  `

**endif-line:**  
 `#endif ` 

**digit-sequence:**  
 digit  
 digit-sequence digit  

**digit:** 
 [0-9]  

**token-string:**  
 String of token  

**token:**  
 keyword  
 identifier  
 constant  
 operator  
 punctuator  

**filename:**  
 Legal operating system filename

