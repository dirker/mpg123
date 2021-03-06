/*
	mpg123_to_wav.c

	copyright 2007-2015 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Nicholas Humfrey
*/

#include <out123.h>
#include <mpg123.h>
#include <stdio.h>
#include <strings.h>

void usage(const char *cmd)
{
	printf("Usage: %s <input> <output> [<encname> [ <buffersize>]]\n", cmd);
	exit(99);
}

void cleanup(mpg123_handle *mh, out123_handle *ao)
{
	out123_del(ao);
	/* It's really to late for error checks here;-) */
	mpg123_close(mh);
	mpg123_delete(mh);
	mpg123_exit();
}

int main(int argc, char *argv[])
{
	mpg123_handle *mh = NULL;
	out123_handle *ao = NULL;
	unsigned char* buffer = NULL;
	size_t buffer_size = 0;
	size_t done = 0;
	int channels = 0;
	int encoding = 0;
	int framesize = 1;
	long rate = 0;
	int  err  = MPG123_OK;
	off_t samples = 0;

	if (argc<3) usage(argv[0]);
	printf( "Input file: %s\n", argv[1]);
	printf( "Output file: %s\n", argv[2]);
	
	err = mpg123_init();
	if(err != MPG123_OK || (mh = mpg123_new(NULL, &err)) == NULL)
	{
		fprintf(stderr, "Basic setup goes wrong: %s", mpg123_plain_strerror(err));
		cleanup(mh, ao);
		return -1;
	}
	ao = out123_new();
	if(!ao)
	{
		fprintf(stderr, "Cannot create output handle.\n");
		cleanup(mh, ao);
		return -1;
	}

	if(argc >= 4)
	{ /* Make mpg123 support the desired encoding only for all rates. */
		const long *rates;
		size_t rate_count;
		size_t i;
		int enc;
		/* If that is zero, you'll get the error soon enough from mpg123. */
		enc = out123_enc_byname(argv[3]);
		mpg123_format_none(mh);
		mpg123_rates(&rates, &rate_count);
		for(i=0; i<rate_count; ++i)
			mpg123_format(mh, rates[i], MPG123_MONO|MPG123_STEREO, enc);
	}

	    /* Let mpg123 work with the file, that excludes MPG123_NEED_MORE messages. */
	if(    mpg123_open(mh, argv[1]) != MPG123_OK
	    /* Peek into track and get first output format. */
	    || mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK )
	{
		fprintf( stderr, "Trouble with mpg123: %s\n", mpg123_strerror(mh) );
		cleanup(mh, ao);
		return -1;
	}
	if(out123_open(ao, "wav", argv[2]) != OUT123_OK)
	{
		fprintf(stderr, "Trouble with out123: %s\n", out123_strerror(ao));
		cleanup(mh, ao);
		return -1;
	}

	/* Ensure that this output format will not change
	   (it might, when we allow it). */
	mpg123_format_none(mh);
	mpg123_format(mh, rate, channels, encoding);

	printf("Creating WAV with %i channels and %liHz, encoding %s.\n"
	,	channels, rate, out123_enc_name(encoding));
	if(  out123_start(ao, rate, channels, encoding)
	  || out123_getformat(ao, NULL, NULL, NULL, &framesize) )
	{
		fprintf(stderr, "Cannot start output / get framesize: %s\n"
		,	out123_strerror(ao));
		cleanup(mh, ao);
		return -1;
	}

	/* Buffer could be almost any size here, mpg123_outblock() is just some recommendation.
	   Important, especially for sndfile writing, is that the size is a multiple of sample size. */
	buffer_size = argc >= 5 ? atol(argv[4]) : mpg123_outblock(mh);
	buffer = malloc( buffer_size );

	do
	{
		size_t played;
		err = mpg123_read( mh, buffer, buffer_size, &done );
		played = out123_play(ao, buffer, done);
		if(played != done)
		{
			fprintf(stderr
			,	"Warning: written less than gotten from libmpg123: %li != %li\n"
			,	(long)played, (long)done);
		}
		samples += played/framesize;
		/* We are not in feeder mode, so MPG123_OK, MPG123_ERR and MPG123_NEW_FORMAT are the only possibilities.
		   We do not handle a new format, MPG123_DONE is the end... so abort on anything not MPG123_OK. */
	} while (done && err==MPG123_OK);

	free(buffer);

	if(err != MPG123_DONE)
	fprintf( stderr, "Warning: Decoding ended prematurely because: %s\n",
	         err == MPG123_ERR ? mpg123_strerror(mh) : mpg123_plain_strerror(err) );

	printf("%li samples written.\n", (long)samples);
	cleanup(mh, ao);
	return 0;
}
