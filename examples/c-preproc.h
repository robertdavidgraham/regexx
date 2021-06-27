#ifndef CPREPROC_H
#define CPREPROC_H

struct preprocessor_t;
typedef struct preprocessor_t preprocessor_t;
struct clex_t;

preprocessor_t *preproc_create(const char *filename, struct clex_t *clex);
int preproc_parse(preprocessor_t *pp);
void preproc_free(preprocessor_t *pp);

#endif

