/*
 This file contains test-cases for the C preprocessor.
 */

#ifdef NOTDEFINED
#error NOTEDEFINED is defined
#else
#warning NOTEDEFINED is not defined
#endif

#define ISDEFINED

#ifdef ISDEFINED
#warning  ISDEFINED  is defined @
#else
#error ISDEFINED is not defined
#endif

// testing \
more testing \
more testing
 \
  \
  //
 //
//

// test comment
/* test'test */
#define FOOB *
#ifdef FOO /* test
test*/
#warning hello
/* 
 */ #endif /* test
test*/

# /*

*/ define foobar 1

#define a c

#define TEST1(a,b) a##b
#define TEST2(x) # x


int main(int argc, char *argv[]) {
    printf("%s\n", TEST2(TEST1(k,l)));
    return 0;
}
