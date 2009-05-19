/*
	reader: reading input data

	copyright ?-2007 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Thomas Orgis (after code from Michael Hipp)
*/

#ifndef MPG123_READER_H
#define MPG123_READER_H

#include "config.h"
#include "mpg123.h"

struct buffy
{
	unsigned char *data;
	ssize_t size;
	struct buffy *next;
};

struct bufferchain
{
	struct buffy* first; /* The beginning of the chain. */
	struct buffy* last;  /* The end...    of the chain. */
	ssize_t size;        /* Aggregated size of all buffies. */
	/* These positions are relative to buffer chain beginning. */
	ssize_t pos;         /* Position in whole chain. */
	ssize_t firstpos;    /* The point of return on non-forget() */
	/* The "real" filepos is fileoff + pos. */
	off_t fileoff;       /* Beginning of chain is at this file offset. */
};

struct reader_data
{
	off_t filelen; /* total file length or total buffer size */
	off_t filepos; /* position in file or position in buffer chain */
	int   filept;
	int   flags;
#ifndef WIN32
	long timeout_sec;
#endif
	ssize_t (*fdread) (mpg123_handle *, void *, size_t);
	/* User can replace the read and lseek functions. The r_* are the stored replacement functions or NULL,
	   The second two pointers are the actual workers (default map to POSIX read/lseek). */
	ssize_t (*r_read) (int fd, void *buf, size_t count);
	off_t   (*r_lseek)(int fd, off_t offset, int whence);
	ssize_t (*read) (int fd, void *buf, size_t count);
	off_t   (*lseek)(int fd, off_t offset, int whence);
	/* Buffered readers want that abstracted, set internally. */
	ssize_t (*fullread)(mpg123_handle *, unsigned char *, ssize_t);
	union
	{
		struct bufferchain buffer; /* Not dynamically allocated, these few struct bytes aren't worth the trouble. */
		struct mpg123_raw_stream raw;
	};
};

/* start to use off_t to properly do LFS in future ... used to be long */
struct reader
{
	int     (*init)           (mpg123_handle *);
	void    (*close)          (mpg123_handle *);
	ssize_t (*fullread)       (mpg123_handle *, unsigned char *, ssize_t);
	int     (*head_read)      (mpg123_handle *, unsigned long *newhead);    /* succ: TRUE, else <= 0 (FALSE or READER_MORE) */
	int     (*head_shift)     (mpg123_handle *, unsigned long *head);       /* succ: TRUE, else <= 0 (FALSE or READER_MORE) */
	off_t   (*skip_bytes)     (mpg123_handle *, off_t len);                 /* succ: >=0, else error or READER_MORE         */
	int     (*read_frame_body)(mpg123_handle *, unsigned char *, int size);
	int     (*back_bytes)     (mpg123_handle *, off_t bytes);
	int     (*seek_frame)     (mpg123_handle *, off_t num);
	off_t   (*tell)           (mpg123_handle *);
	void    (*rewind)         (mpg123_handle *);
	void    (*forget)         (mpg123_handle *);
};

/* Open a file by path or use an opened file descriptor. */
int open_stream(mpg123_handle *, const char *path, int fd);

/* feed based operation has some specials */
int open_feed(mpg123_handle *);
/* externally called function, returns 0 on success, -1 on error */
int  feed_more(mpg123_handle *fr, const unsigned char *in, long count);
void feed_forget(mpg123_handle *fr);  /* forget the data that has been read (free some buffers) */
off_t feed_set_pos(mpg123_handle *fr, off_t pos); /* Set position (inside available data if possible), return wanted byte offset of next feed. */

/* raw performance interface */
int open_raw(mpg123_handle *);
int  raw_more(mpg123_handle *fr, const unsigned char *in, long count);

void open_bad(mpg123_handle *);

#define READER_FD_OPENED 0x1
#define READER_ID3TAG    0x2
#define READER_SEEKABLE  0x4
#define READER_BUFFERED  0x8
#define READER_NONBLOCK  0x20

#define READER_STREAM 0
#define READER_ICY_STREAM 1
#define READER_FEED       2
#define READER_RAW		  3
/* These two add a little buffering to enable small seeks for peek ahead. */
#define READER_BUF_STREAM 4
#define READER_BUF_ICY_STREAM 5

#ifdef READ_SYSTEM
#define READER_SYSTEM 6
#define READERS 7
#else
#define READERS 6
#endif

#define READER_ERROR MPG123_ERR
#define READER_MORE  MPG123_NEED_MORE

#endif
