#ifndef CPREPROC_H
#define CPREPROC_H

struct translationunit_t;
typedef struct translationunit_t translationunit_t;
struct clex_t;

translationunit_t *preproc_create(const char *filename, struct clex_t *clex);
int preproc_parse(translationunit_t *pp);
void preproc_free(translationunit_t *pp);

#endif

