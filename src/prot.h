#pragma once

struct prot;
typedef struct prot prot_t;

#define MAXBUFLEN 2000
#define MAXARGS   20
#define FS        ' '
#define RS        '\n'
#define ESC       '\\'


extern
int
prot_create(
	prot_t **prot
);

extern
void
prot_destroy(
	prot_t *prot
);

extern
void
prot_reset(
	prot_t *prot
);

extern
int
prot_put(
	prot_t *prot,
	unsigned count,
	const char **fields
);

extern
int
prot_putx(
	prot_t *prot,
	...
);

extern
int
prot_should_write(
	prot_t *prot
);

extern
int
prot_write(
	prot_t *prot,
	int fdout
);

extern
int
prot_can_read(
	prot_t *prot
);

extern
int
prot_read(
	prot_t *prot,
	int fdin
);

extern
int
prot_get(
	prot_t *prot,
	const char ***fields
);

extern
void
prot_next(
	prot_t *prot
);


