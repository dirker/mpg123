/*
	frame: Central data structures and opmitization hooks.

	copyright 2007 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Thomas Orgis
*/

#ifndef MPG123_FRAME_H
#define MPG123_FRAME_H

#include <stdio.h>
#include "config.h"
#include "mpg123.h"
#include "optimize.h"
#include "id3.h"
#include "icy.h"
#include "reader.h"
#ifdef FRAME_INDEX
#include "index.h"
#endif
#include "synths.h"

#ifdef OPT_DITHER
#include "dither.h"
int frame_dither_init(mpg123_handle *fr);
#endif

/* max = 1728 */
#define MAXFRAMESIZE 3456

struct al_table
{
  short bits;
  short d;
};

/* the output buffer, used to be pcm_sample, pcm_point and audiobufsize */
struct outbuffer
{
	unsigned char *data;
	unsigned char *p; /* read pointer  */
	size_t fill; /* fill from read pointer */
	size_t size; /* that's actually more like a safe size, after we have more than that, flush it */
};

void invalidate_format(struct mpg123_audioformat *af);

struct mpg123_pars_struct
{
	int verbose;    /* verbose level */
	long flags; /* combination of above */
#ifndef NO_NTOM
	long force_rate;
#endif
	int down_sample;
	int rva; /* (which) rva to do: 0: nothing, 1: radio/mix/track 2: album/audiophile */
	long halfspeed;
	long doublespeed;
#if (!defined (WIN32) || defined (__CYGWIN__))
	long timeout;
#endif
#define NUM_CHANNELS 2
	char audio_caps[NUM_CHANNELS][MPG123_RATES+1][MPG123_ENCODINGS];
/*	long start_frame; */ /* frame offset to begin with */
/*	long frame_number;*/ /* number of frames to decode */
#ifndef NO_ICY
	long icy_interval;
#endif
	double outscale;
	long resync_limit;
	long index_size; /* Long, because: negative values have a meaning. */
	long preframes;
};



/* There is a lot to condense here... many ints can be merged as flags; though the main space is still consumed by buffers. */
struct mpg123_handle_struct
{
	struct mpg123_raw_state ps; /* public state */

	int fresh; /* to be moved into flags */
	real hybrid_block[2][2][SBLIMIT*SSLIMIT];
	int hybrid_blc[2];
	/* the scratch vars for the decoders, sometimes real, sometimes short... sometimes int/long */ 
	short *short_buffs[2][2];
	real *real_buffs[2][2];
	unsigned char *rawbuffs;
	int rawbuffss;
#ifdef OPT_I486
	int i486bo[2];
#endif
	int bo; /* Just have it always here. */
#ifdef OPT_DITHER
	int ditherindex;
	float *dithernoise;
#endif
	unsigned char* rawdecwin; /* the block with all decwins */
	int rawdecwins; /* size of rawdecwin memory */
	real *decwin; /* _the_ decode table */
#ifdef OPT_MMXORSSE
	/* I am not really sure that I need both of them... used in assembler */
	float *decwin_mmx;
	float *decwins;
#endif
	int have_eq_settings;
	real equalizer[2][32];

	/* for halfspeed mode */
	unsigned char ssave[34];
	int halfphase;
#ifndef NO_8BIT
	/* a raw buffer and a pointer into the middle for signed short conversion, only allocated on demand */
	unsigned char *conv16to8_buf;
	unsigned char *conv16to8;
#endif
	/* There's some possible memory saving for stuff that is not _really_ dynamic. */

	/* layer3 */
	int longLimit[9][23];
	int shortLimit[9][14];
	real gainpow2[256+118+4]; /* not really dynamic, just different for mmx */

	/* layer2 */
	real muls[27][64];	/* also used by layer 1 */

#ifndef NO_NTOM
	/* decode_ntom */
	unsigned long ntom_val[2];
	unsigned long ntom_step;
#endif
	/* special i486 fun */
#ifdef OPT_I486
	int *int_buffs[2][2];
#endif
	/* special altivec... */
#ifdef OPT_ALTIVEC
	real *areal_buffs[4][4];
#endif
	struct synth_s synths;
	struct
	{
#ifdef OPT_MULTI

#ifndef NO_LAYER3
#if (defined OPT_3DNOW || defined OPT_3DNOWEXT)
		void (*dct36)(real *,real *,real *,real *,real *);
#endif
#endif

#endif
		enum optdec type;
		enum optcla class;
	} cpu_opts;

	int verbose;    /* 0: nothing, 1: just print chosen decoder, 2: be verbose */

	const struct al_table *alloc;
	/* The runtime-chosen decoding, based on input and output format. */
	func_synth synth;
	func_synth_stereo synth_stereo;
	func_synth_mono synth_mono;
	/* Yes, this function is runtime-switched, too. */
	void (*make_decode_tables)(mpg123_handle *fr); /* That is the volume control. */
	int bitrate_index;
	int sampling_frequency;
	int stereo; /* I _think_ 1 for mono and 2 for stereo */
	int jsbound;
#define SINGLE_STEREO -1
#define SINGLE_LEFT    0
#define SINGLE_RIGHT   1
#define SINGLE_MIX     3
	int single;
	int II_sblimit;
	int down_sample_sblimit;
	/* Many flags in disguise as integers... wasting bytes. */
	int down_sample;
	int header_change;
	int (*do_layer)(mpg123_handle *);
	off_t playnum; /* playback offset... includes repetitions, reset at seeks */
	char accurate; /* Flag to see if we trust the frame number. */
	char silent_resync; /* Do not complain for the next n resyncs. */
	unsigned char* xing_toc; /* The seek TOC from Xing header. */
	long freeformat_framesize;

	/* bitstream info; bsi */
	int bitindex;
	unsigned char *wordpointer;
	/* temporary storage for getbits stuff */
	unsigned long ultmp;
	unsigned char uctmp;

	int abr_rate;			/* The target average bitrate. */

	/* rva data, used in common.c, set in id3.c */

	double lastscale;
	struct
	{
		int level[2];
		float gain[2];
		float peak[2];
	} rva;

	/* input data */
	off_t track_samples;
	double mean_framesize;
	off_t mean_frames;
	int fsizeold;
	int ssize;
	unsigned int bitreservoir;
	unsigned char bsspace[2][MAXFRAMESIZE+512]; /* MAXFRAMESIZE */
	unsigned char *bsbuf;
	unsigned char *bsbufold;
	int bsnum;
	unsigned long oldhead;
	unsigned long firsthead;
#ifdef FRAME_INDEX
	struct frame_index index;
#endif

	/* output data */
	struct outbuffer buffer;
	int own_buffer;
	size_t outblock; /* number of bytes that this frame produces (upper bound) */
	int to_decode;   /* this frame holds data to be decoded */
	int to_ignore;   /* the same, somehow */
	off_t firstframe;  /* start decoding from here */
	off_t lastframe;   /* last frame to decode (for gapless or num_frames limit) */
	off_t ignoreframe; /* frames to decode but discard before firstframe */
#ifdef GAPLESS
	off_t firstoff; /* number of samples to ignore from firstframe */
	off_t lastoff;  /* number of samples to use from lastframe */
	off_t begin_s;  /* overall begin offset in samples */
	off_t begin_os;
	off_t end_s;    /* overall end offset in samples */
	off_t end_os;
#endif
	struct reader *rd; /* pointer to the reading functions */
	struct reader_data rdat; /* reader data and state info */
	struct mpg123_pars_struct p;
	int decoder_change;
	long clip;
	/* the meta crap */
	int metaflags;
	unsigned char id3buf[128];
#ifndef NO_ID3V2
	mpg123_id3v2 id3v2;
#endif
#ifndef NO_ICY
	struct icy_meta icy;
#endif
};

/* generic init, does not include dynamic buffers */
void frame_init(mpg123_handle *fr);
void frame_init_par(mpg123_handle *fr, mpg123_pars *mp);
/* output buffer and format */
int  frame_outbuffer(mpg123_handle *fr);
int  frame_output_format(mpg123_handle *fr);

int frame_buffers(mpg123_handle *fr); /* various decoder buffers, needed once */
int frame_reset(mpg123_handle* fr);   /* reset for next track */
int frame_buffers_reset(mpg123_handle *fr);
void frame_exit(mpg123_handle *fr);   /* end, free all buffers */

/* Index functions... */
/* Well... print it... */
int mpg123_print_index(mpg123_handle *fr, FILE* out);
/* Find a seek position in index. */
off_t frame_index_find(mpg123_handle *fr, off_t want_frame, off_t* get_frame);
/* Apply index_size setting. */
int frame_index_setup(mpg123_handle *fr);

void do_volume(mpg123_handle *fr, double factor);
void do_rva(mpg123_handle *fr);

/* samples per frame ...
Layer I
Layer II
Layer III
MPEG-1
384
1152
1152
MPEG-2 LSF
384
1152
576
MPEG 2.5
384
1152
576
*/
#define spf(fr) ((fr)->ps.lay == 1 ? 384 : ((fr)->ps.lay==2 ? 1152 : ((fr)->ps.lsf || (fr)->ps.mpeg25 ? 576 : 1152)))

#ifdef GAPLESS
/* well, I take that one for granted... at least layer3 */
#define GAPLESS_DELAY 529
/* still fine-tuning the "real music" window... see read_frame */
void frame_gapless_init(mpg123_handle *fr, off_t b, off_t e);
void frame_gapless_realinit(mpg123_handle *fr);
void frame_gapless_update(mpg123_handle *mh, off_t total_samples);
/*void frame_gapless_position(mpg123_handle* fr);
void frame_gapless_bytify(mpg123_handle *fr);
void frame_gapless_ignore(mpg123_handle *fr, off_t frames);*/
/* void frame_gapless_buffercheck(mpg123_handle *fr); */
#endif

/* Number of samples the decoding of the current frame should yield. */
off_t frame_expect_outsamples(mpg123_handle *fr);

/* Skip this frame... do some fake action to get away without actually decoding it. */
void frame_skip(mpg123_handle *fr);

/*
	Seeking core functions:
	- convert input sample offset to output sample offset
	- convert frame offset to output sample offset
	- get leading frame offset for output sample offset
	The offsets are "unadjusted"/internal; resampling is being taken care of.
*/
off_t frame_ins2outs(mpg123_handle *fr, off_t ins);
off_t frame_outs(mpg123_handle *fr, off_t num);
/* This one just computes the expected sample count for _this_ frame. */
off_t frame_expect_outsampels(mpg123_handle *fr);
off_t frame_offset(mpg123_handle *fr, off_t outs);
void frame_set_frameseek(mpg123_handle *fr, off_t fe);
void frame_set_seek(mpg123_handle *fr, off_t sp);
off_t frame_tell_seek(mpg123_handle *fr);
/* Take a copy of the Xing VBR TOC for fuzzy seeking. */
int frame_fill_toc(mpg123_handle *fr, unsigned char* in);


/* adjust volume to current outscale and rva values if wanted */
void do_rva(mpg123_handle *fr);
#endif