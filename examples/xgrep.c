#include "../src/regexx.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <direct.h>

/**
 * Searchs lines of text in the file for pattern match, printing those
 * lines that match
 * @return number of matches
 */
size_t parse_file(regexx_t *re, FILE *fp) {
    char line[2048];
    size_t found_count = 0;


    while (fgets(line, sizeof(line), fp)) {
        size_t line_length = strlen(line);
        size_t result;
        size_t offset = 0;
        size_t length = 0;
        bool is_found = false;

        while (offset < line_length) {
            result = regexx_match(re, line, offset, line_length, &offset, &length);
            if (result == REGEXX_NOT_FOUND)
                break;
            fprintf(stdout, "[%.*s] ", (unsigned)length, line+offset);
            is_found = true;
            offset = offset + length;
        }
        if (is_found) {
            printf("\n");
            found_count++;
        }
    } 

    return found_count;
}

int main(int argc, char *argv[]) {
    regexx_t *re;
    int err;
    size_t found_count = 0;
 
    printf("%s\n", _getcwd(0,0));
    if (argc <= 1) {
        fprintf(stderr, "[-] first parameter must be regex pattern\n");
        return -1;
    }
    
    /*
     * Parse regular-expression
     * - on failure, print an error message
     * - on success, print what we parsed, which may differ slightly from
     *   the original input
     */
    re = regexx_create(0);
    err = regexx_add_pattern(re, argv[1], 0, 0);
    if (err) {
        /* Malformed input regexp */
        fprintf(stderr, "[-] %s\n", regexx_get_error_msg(re));
        regexx_free(re);
        return -1;
    } else {
        size_t i;
        
        /* Print all regexps, there can be more than one */
        for (i=0; ; i++) {
            size_t id = 0;
            char *buf;
                
            buf = regexx_print(re, i, &id, 0);
            if (buf == NULL)
                break;
            fprintf(stderr, "[+] regex = /%s/\n", buf);
            free(buf);
        }
    }

    
    if (argc == 2) {
        fprintf(stderr, "[+] reading from <stdin>\n");
        found_count += parse_file(re, stdin);
    } else {
        int i;

        for (i=2; i<argc; i++) {
            FILE *fp = fopen(argv[i], "rb");
            if (fp == NULL) {
                fprintf(stderr, "[-] %s: %s\n", argv[i], strerror(errno));
                continue;
            }
            parse_file(re, fp);
            fclose(fp);
        }
    }

    regexx_free(re);

    return found_count;
}

