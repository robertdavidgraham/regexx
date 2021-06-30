#include "c-preproc.h"
#include <stdio.h>
#include <unistd.h>

#

void tests(void) {
    /* should print 123 */
    #define function() 123
    #define concat(a,b) a ## b
    printf("%u\n", concat(func,tion)());
}

int main(int argc, char *argv[]) {
    int i;
    
    printf("cwd = %s\n", getcwd(0,0));
    
    for (i=1; i<argc; i++) {
        int err;
        translationunit_t *pp;
        pp = preproc_create(argv[i], 0);
        if (pp == NULL) {
            fprintf(stderr, "[-] preprocessor: failed\n");
            preproc_free(pp);
            goto fail;
        }
        
        err = preproc_parse(pp);
        if (err != 0) {
            fprintf(stderr, "[-] %s: failed to preprocess\n", argv[i]);
            preproc_free(pp);
            goto fail;
        }
        
        preproc_free(pp);
    }
    
    return 0;
fail:
    return -1;
}
