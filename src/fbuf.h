#pragma once

/**
 * A fbuf records file data and access
 */
struct fbuf
{
        /** filename for messages */
        const char *name;

        /** in memory copy of the file */
        void *buffer;

        /** size saved to the file */
        uint32_t saved;

        /** size currently used */
        uint32_t used;

        /** size currently allocated */
        uint32_t capacity;

        /** size of the file */
        uint32_t size;

        /** opened file descriptor for the file */
        int fd;
};

/** short type */
typedef struct fbuf fbuf_t;


/** open in 'fb' the file of 'name' */
extern
int
fbuf_open(
	fbuf_t	*fb,
	const char *name
);

/** close the file 'fb' */
extern
void
fbuf_close(
	fbuf_t	*fb
);

/** write to file 'fb' at 'offset' the 'count' bytes pointed by 'buffer' */
extern
int
fbuf_write(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count,
	uint32_t offset
);

/** write to file 'fb' the unsaved bytes and flush the content to the file */
extern
int
fbuf_sync(
	fbuf_t	*fb
);

/** allocate enough memory in 'fb' to store 'count' bytes */
extern
int
fbuf_ensure_capacity(
	fbuf_t	*fb,
	uint32_t count
);

/** put at 'offset' in the memory of 'fb' the 'count' bytes pointed by 'buffer' */
extern
int
fbuf_put(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count,
	uint32_t offset
);

/** append at end in the memory of 'fb' the 'count' bytes pointed by 'buffer' */
extern
int
fbuf_append(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count
);

/** check or make identification of file 'fb' by 'id' of 'len' */
extern
int
fbuf_identify(
	fbuf_t	*fb,
	const char *id,
	uint32_t idlen
);

/** check or make identification by 'uuid' of file 'fb' */
extern
int
fbuf_open_identify(
	fbuf_t	*fb,
	const char *name,
	const char *id,
	uint32_t idlen
);

