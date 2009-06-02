/*
	audio: audio output interface

	copyright ?-2006 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.de
	initially written by Michael Hipp
*/


#ifndef _MPG123_AUDIO_H_
#define _MPG123_AUDIO_H_

#include "module.h"


/* Obsolete - due to be removed */
enum {
	DECODE_TEST,
	DECODE_AUDIO,
	DECODE_FILE,
	DECODE_BUFFER,
	DECODE_WAV,
	DECODE_AU,
	DECODE_CDR,
	DECODE_AUDIOFILE
};


#define AUDIO_FORMAT_MASK			0x100
#define AUDIO_FORMAT_16				0x100
#define AUDIO_FORMAT_8				0x000

#define AUDIO_FORMAT_SIGNED_16		0x110
#define AUDIO_FORMAT_UNSIGNED_16	0x120
#define AUDIO_FORMAT_UNSIGNED_8		0x1
#define AUDIO_FORMAT_SIGNED_8		0x2
#define AUDIO_FORMAT_ULAW_8			0x4
#define AUDIO_FORMAT_ALAW_8			0x8

/* 3% rate tolerance */
#define AUDIO_RATE_TOLERANCE	  3



typedef struct audio_output_struct
{
	int fn;			/* filenumber */
	void *userptr;	/* driver specific pointer */
	
	/* Callbacks */
	int (*open)(struct audio_output_struct *);
	int (*get_formats)(struct audio_output_struct *);
	int (*write)(struct audio_output_struct *, unsigned char *,int);
	void (*flush)(struct audio_output_struct *);
	int (*close)(struct audio_output_struct *);
	int (*deinit)(struct audio_output_struct *);
	
	/* the module this belongs to */
	mpg123_module_t *module;
	
	char *device;	/* device name */
	long rate;		/* sample rate */
	long gain;		/* output gain */
	int channels;	/* number of channels */
	int format;		/* format flags */
	
} audio_output_t;

struct audio_format_name {
	int  val;
	char *name;
	char *sname;
};


/* ------ Declarations from "audio.c" ------ */

extern audio_output_t* open_output_module( const char* name );
extern audio_output_t* alloc_audio_output();
extern void deinit_audio_output( audio_output_t* ao );
extern void audio_output_dump(audio_output_t *ao);
extern void audio_capabilities(audio_output_t *ao);
extern int audio_fit_capabilities(audio_output_t *ao,int c,int r);
extern char *audio_encoding_name(int format);

extern int init_output( audio_output_t *ao );


#endif
