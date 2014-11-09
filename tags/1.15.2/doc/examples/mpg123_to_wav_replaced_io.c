/*
	mpg123_to_wav.c

	copyright 2007-2010 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Nicholas Humfrey (moved to handle I/O by Thomas Orgis)

	This example program demonstrates how to use libmpg123 to decode a file to WAV (writing via libsndfile), while doing the I/O (read and seek) with custom callback functions.
	This should cater for any situation where you have some special means to get to the data (like, mmapped files / plain buffers in memory, funky network streams).

	Disregarding format negotiations, the basic synopsis is:

	mpg123_init()
	mpg123_new()
	mpg123_replace_reader_handle()

	mpg123_open_handle()
	mpg123_read()
	mpg123_close()

	mpg123_delete()
	mpg123_exit()
*/

#include <stdio.h>
#include <strings.h>
#include <mpg123.h>
#include <sndfile.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void usage()
{
	printf("Usage: mpg123_to_wav <input> <output>\n");
	exit(99);
}

void cleanup(mpg123_handle *mh)
{
	/* It's really to late for error checks here;-) */
	mpg123_close(mh);
	mpg123_delete(mh);
	mpg123_exit();
}

/* Simple handle for you private I/O data. */
struct ioh { int fd; };

/* The callback functions; simple wrappers over standard I/O.
   They could be anything you like... */

static ssize_t read_cb(void *handle, void *buf, size_t sz)
{
	ssize_t ret;
	struct ioh *h = handle;
	errno = 0;
	ret = read(h->fd, buf, sz);
	if(ret < 0) fprintf(stderr, "read error: %s\n", strerror(errno));

	return ret;
}

static off_t lseek_cb(void *handle, off_t offset, int whence)
{
	off_t ret;
	struct ioh *h = handle;
	ret = lseek(h->fd, offset, whence);
	if(ret < 0) fprintf(stderr, "seek error: %s\n", strerror(errno));

	return ret;
}

/* The cleanup handler is called on mpg123_close(), it can cleanup your part of the mess... */
void cleanup_cb(void *handle)
{
	struct ioh *h = handle;
	close(h->fd);
	h->fd = -1;
}


int main(int argc, char *argv[])
{
	SNDFILE* sndfile = NULL;
	SF_INFO sfinfo;
	mpg123_handle *mh = NULL;
	unsigned char* buffer = NULL;
	size_t buffer_size = 0;
	size_t done = 0;
	int  channels = 0, encoding = 0;
	long rate = 0;
	int  err  = MPG123_OK;
	off_t samples = 0;
	struct ioh *iohandle;

	if (argc!=3) usage();
	printf( "Input file: %s\n", argv[1]);
	printf( "Output file: %s\n", argv[2]);

	err = mpg123_init();

	errno = 0;
	iohandle = malloc(sizeof(struct ioh));
	iohandle->fd = open(argv[1], O_RDONLY);
	if(iohandle->fd < 0)
	{
		fprintf(stderr, "Cannot open input file (%s).\n", strerror(errno));
		return -1;
	}

	if( err != MPG123_OK || (mh = mpg123_new(NULL, &err)) == NULL
	    /* Let mpg123 work with the file, that excludes MPG123_NEED_MORE messages. */
	    || mpg123_replace_reader_handle(mh, read_cb, lseek_cb, cleanup_cb) != MPG123_OK
	    || mpg123_open_handle(mh, iohandle) != MPG123_OK
	    /* Peek into track and get first output format. */
	    || mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK )
	{
		fprintf( stderr, "Trouble with mpg123: %s\n",
		         mh==NULL ? mpg123_plain_strerror(err) : mpg123_strerror(mh) );
		cleanup(mh);
		return -1;
	}

	if(encoding != MPG123_ENC_SIGNED_16)
	{ /* Signed 16 is the default output format anyways; it would actually by only different if we forced it.
	     So this check is here just for this explanation. */
		cleanup(mh);
		fprintf(stderr, "Bad encoding: 0x%x!\n", encoding);
		return -2;
	}
	/* Ensure that this output format will not change (it could, when we allow it). */
	mpg123_format_none(mh);
	mpg123_format(mh, rate, channels, encoding);

	/* Buffer could be almost any size here, mpg123_outblock() is just some recommendation.
	   Important, especially for sndfile writing, is that the size is a multiple of sample size. */
	buffer_size = mpg123_outblock( mh );
	buffer = malloc( buffer_size );

	bzero(&sfinfo, sizeof(sfinfo) );
	sfinfo.samplerate = rate;
	sfinfo.channels = channels;
	sfinfo.format = SF_FORMAT_WAV|SF_FORMAT_PCM_16;
	printf("Creating 16bit WAV with %i channels and %liHz.\n", channels, rate);

	sndfile = sf_open(argv[2], SFM_WRITE, &sfinfo);
	if(sndfile == NULL){ fprintf(stderr, "Cannot open output file!\n"); cleanup(mh); return -2; }

	do
	{
		err = mpg123_read( mh, buffer, buffer_size, &done );
		sf_write_short( sndfile, (short*)buffer, done/sizeof(short) );
		samples += done/sizeof(short);
		/* We are not in feeder mode, so MPG123_OK, MPG123_ERR and MPG123_NEW_FORMAT are the only possibilities.
		   We do not handle a new format, MPG123_DONE is the end... so abort on anything not MPG123_OK. */
	} while (err==MPG123_OK);

	if(err != MPG123_DONE)
	fprintf( stderr, "Warning: Decoding ended prematurely because: %s\n",
	         err == MPG123_ERR ? mpg123_strerror(mh) : mpg123_plain_strerror(err) );

	sf_close( sndfile );

	samples /= channels;
	printf("%li samples written.\n", (long)samples);
	cleanup(mh);
	free(iohandle);
	return 0;
}