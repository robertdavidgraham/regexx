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
#endif int x =


int main(int argc, char *argv[]) {
    return 0;
}
