/*
	mpg123: main code of the program (not of the decoder...)

	copyright 1995-2006 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Michael Hipp
*/

#define ME "main"
#include "mpg123app.h"
#include "mpg123.h"
#include <stdlib.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

/* be paranoid about setpriority support */
#ifndef PRIO_PROCESS
#undef HAVE_SETPRIORITY
#endif

#include "common.h"
#include "getlopt.h"
#include "buffer.h"
#include "term.h"
#include "playlist.h"
#include "id3print.h"
#include "getbits.h"
#include "httpget.h"
#include "getcpuflags.h"

static void usage(int err);
static void want_usage(char* arg);
static void long_usage(int err);
static void want_long_usage(char* arg);
static void print_title(FILE* o);
static void give_version(char* arg);

struct parameter param = { 
  FALSE , /* aggressiv */
  FALSE , /* shuffle */
  FALSE , /* remote */
  FALSE , /* remote to stderr */
  DECODE_AUDIO , /* write samples to audio device */
  FALSE , /* silent operation */
  FALSE , /* xterm title on/off */
  0 ,     /* second level buffer size */
  0 ,     /* verbose level */
#ifdef HAVE_TERMIOS
  FALSE , /* term control */
#endif
  FALSE , /* checkrange */
  0 ,	  /* force_reopen, always (re)opens audio device for next song */
  /* test_cpu flag is valid for multi and 3dnow.. even if 3dnow is built alone; ensure it appears only once */
  FALSE , /* normal operation */
  FALSE,  /* try to run process in 'realtime mode' */   
  { 0,},  /* wav,cdr,au Filename */
	0, /* default is to play all titles in playlist */
	NULL, /* no playlist per default */
	0 /* condensed id3 per default */
	,0 /* list_cpu */
	,NULL /* cpu */ 
#ifdef FIFO
	,NULL
#endif
	/* Parameters for mpg123 handle, defaults are queried from library! */
	,0 /* down_sample */
	,0 /* rva */
	,0 /* halfspeed */
	,0 /* doublespeed */
	,0 /* start_frame */
	,-1 /* frame_number */
	,0 /* outscale */
	,0 /* flags */
	,0 /* force_rate */
	,1 /* ICY */
};

mpg123_handle *mh = NULL;
off_t framenum;
off_t frames_left;
struct audio_info_struct ai;
txfermem *buffermem = NULL;
char *prgName = NULL;
char *equalfile = NULL;
/* ThOr: pointers are not TRUE or FALSE */
struct httpdata htd;

int buffer_fd[2];
int buffer_pid;
static size_t bufferblock = 0;

static int intflag = FALSE;

int OutputDescriptor;

#if !defined(WIN32) && !defined(GENERIC)
#ifndef NOXFERMEM
static void catch_child(void)
{
  while (waitpid(-1, NULL, WNOHANG) > 0);
}
#endif

static void catch_interrupt(void)
{
  intflag = TRUE;
}
#endif

/* oh, what a mess... */
void next_track(void)
{
	intflag = TRUE;
}

void safe_exit(int code)
{
#ifdef HAVE_TERMIOS
	if(param.term_ctrl)
		term_restore();
#endif
	if(mh != NULL) mpg123_delete(mh);
	mpg123_exit();
	httpdata_reset(&htd);
	exit(code);
}

/* returns 1 if reset_audio needed instead */
int init_output(void)
{
	static int init_done = FALSE;

	if (init_done) return 1;
	init_done = TRUE;
#ifndef NOXFERMEM
	/*
	* Only DECODE_AUDIO and DECODE_FILE are sanely handled by the
	* buffer process. For now, we just ignore the request
	* to buffer the output. [dk]
	*/
	if (param.usebuffer && (param.outmode != DECODE_AUDIO) &&
	(param.outmode != DECODE_FILE)) {
	fprintf(stderr, "Sorry, won't buffer output unless writing plain audio.\n");
	param.usebuffer = 0;
	} 

	if (param.usebuffer)
	{
		unsigned int bufferbytes;
		sigset_t newsigset, oldsigset;
		bufferbytes = (param.usebuffer * 1024);
		if (bufferbytes < bufferblock)
		{
			bufferbytes = 2*bufferblock;
			if(!param.quiet) fprintf(stderr, "Note: raising buffer to minimal size %liKiB\n", (unsigned long) bufferbytes>>10);
		}
		bufferbytes -= bufferbytes % bufferblock;
		/* No +1024 for NtoM rounding problems anymore! */
		xfermem_init (&buffermem, bufferbytes ,0,0);
		mpg123_replace_buffer(mh, (unsigned char *) buffermem->data, bufferblock);
		sigemptyset (&newsigset);
		sigaddset (&newsigset, SIGUSR1);
		sigprocmask (SIG_BLOCK, &newsigset, &oldsigset);
#if !defined(WIN32) && !defined(GENERIC)
		catchsignal (SIGCHLD, catch_child);
#endif
		switch ((buffer_pid = fork()))
		{
			case -1: /* error */
			perror("fork()");
			safe_exit(1);
			case 0: /* child */
			/* oh, is that trouble here? well, buffer should actually be opened before loading tracks IMHO */
			mpg123_close(mh); /* child doesn't need the input stream */
			xfermem_init_reader (buffermem);
			buffer_loop (&ai, &oldsigset);
			xfermem_done_reader (buffermem);
			xfermem_done (buffermem);
			exit(0);
			default: /* parent */
			xfermem_init_writer (buffermem);
			param.outmode = DECODE_BUFFER;
		}
	}
#endif
	/* Open audio if not decoding to buffer */
	switch(param.outmode)
	{
		case DECODE_AUDIO:
			if(audio_open(&ai) < 0){ perror("audio"); safe_exit(1); }
		break;
		case DECODE_WAV:
			wav_open(&ai,param.filename);
		break;
		case DECODE_AU:
			au_open(&ai,param.filename);
		break;
		case DECODE_CDR:
			cdr_open(&ai,param.filename);
		break;
	}
	return 0;
}

static void set_output_h(char *a)
{
  if(ai.output <= 0)
    ai.output = AUDIO_OUT_HEADPHONES;
  else
    ai.output |= AUDIO_OUT_HEADPHONES;
}
static void set_output_s(char *a)
{
  if(ai.output <= 0)
    ai.output = AUDIO_OUT_INTERNAL_SPEAKER;
  else
    ai.output |= AUDIO_OUT_INTERNAL_SPEAKER;
}
static void set_output_l(char *a)
{
  if(ai.output <= 0)
    ai.output = AUDIO_OUT_LINE_OUT;
  else
    ai.output |= AUDIO_OUT_LINE_OUT;
}

static void set_output (char *arg)
{
    switch (*arg) {
        case 'h': set_output_h(arg); break;
        case 's': set_output_s(arg); break;
        case 'l': set_output_l(arg); break;
        default:
            error3("%s: Unknown argument \"%s\" to option \"%s\".\n",
                prgName, arg, loptarg);
            safe_exit(1);
    }
}

void set_verbose (char *arg)
{
    param.verbose++;
}
void set_wav(char *arg)
{
  param.outmode = DECODE_WAV;
  strncpy(param.filename,arg,255);
  param.filename[255] = 0;
}
void set_cdr(char *arg)
{
  param.outmode = DECODE_CDR;
  strncpy(param.filename,arg,255);
  param.filename[255] = 0;
}
void set_au(char *arg)
{
  param.outmode = DECODE_AU;
  strncpy(param.filename,arg,255);
  param.filename[255] = 0;
}
static void SetOutFile(char *Arg)
{
	param.outmode=DECODE_FILE;
	#ifdef WIN32
	OutputDescriptor=_open(Arg,_O_CREAT|_O_WRONLY|_O_BINARY|_O_TRUNC,0666);
	#else
	OutputDescriptor=open(Arg,O_CREAT|O_WRONLY|O_TRUNC,0666);
	#endif
	if(OutputDescriptor==-1)
	{
		error2("Can't open %s for writing (%s).\n",Arg,strerror(errno));
		safe_exit(1);
	}
}
static void SetOutStdout(char *Arg)
{
	param.outmode=DECODE_FILE;
	param.remote_err=TRUE;
	OutputDescriptor=STDOUT_FILENO;
	#ifdef WIN32
	_setmode(STDOUT_FILENO, _O_BINARY);
	#endif
}
static void SetOutStdout1(char *Arg)
{
  param.outmode=DECODE_AUDIOFILE;
	param.remote_err=TRUE;
  OutputDescriptor=STDOUT_FILENO;
	#ifdef WIN32
	_setmode(STDOUT_FILENO, _O_BINARY);
	#endif
}

#ifndef HAVE_SCHED_SETSCHEDULER
static void realtime_not_compiled(char *arg)
{
  fprintf(stderr,"Option '-T / --realtime' not compiled into this binary.\n");
}
#endif

static int frameflag; /* ugly, but that's the way without hacking getlopt */
static void set_frameflag(char *arg)
{
	/* Only one mono flag at a time! */
	if(frameflag & MPG123_FORCE_MONO) param.flags &= ~MPG123_FORCE_MONO;
	param.flags |= frameflag;
}

/* Please note: GLO_NUM expects point to LONG! */
/* ThOr:
 *  Yeah, and despite that numerous addresses to int variables were 
passed.
 *  That's not good on my Alpha machine with int=32bit and long=64bit!
 *  Introduced GLO_INT and GLO_LONG as different bits to make that clear.
 *  GLO_NUM no longer exists.
 */
#ifdef OPT_3DNOW
static int dnow = 0; /* helper for mapping the old 3dnow options */
#endif
topt opts[] = {
	{'k', "skip",        GLO_ARG | GLO_LONG, 0, &param.start_frame, 0},
	{'a', "audiodevice", GLO_ARG | GLO_CHAR, 0, &ai.device,  0},
	{'2', "2to1",        GLO_INT,  0, &param.down_sample, 1},
	{'4', "4to1",        GLO_INT,  0, &param.down_sample, 2},
	{'t', "test",        GLO_INT,  0, &param.outmode, DECODE_TEST},
	{'s', "stdout",      GLO_INT,  SetOutStdout, &param.outmode, DECODE_FILE},
	{'S', "STDOUT",      GLO_INT,  SetOutStdout1, &param.outmode,DECODE_AUDIOFILE},
	{'O', "outfile",     GLO_ARG | GLO_CHAR, SetOutFile, NULL, 0},
	{'c', "check",       GLO_INT,  0, &param.checkrange, TRUE},
	{'v', "verbose",     0,        set_verbose, 0,           0},
	{'q', "quiet",       GLO_INT,  0, &param.quiet, TRUE},
	{'y', "resync",      GLO_INT,  set_frameflag, &frameflag, MPG123_NO_RESYNC},
	{'0', "single0",     GLO_INT,  set_frameflag, &frameflag, MPG123_MONO_LEFT},
	{0,   "left",        GLO_INT,  set_frameflag, &frameflag, MPG123_MONO_LEFT},
	{'1', "single1",     GLO_INT,  set_frameflag, &frameflag, MPG123_MONO_RIGHT},
	{0,   "right",       GLO_INT,  set_frameflag, &frameflag, MPG123_MONO_RIGHT},
	{'m', "singlemix",   GLO_INT,  set_frameflag, &frameflag, MPG123_MONO_MIX},
	{0,   "mix",         GLO_INT,  set_frameflag, &frameflag, MPG123_MONO_MIX},
	{0,   "mono",        GLO_INT,  set_frameflag, &frameflag, MPG123_MONO_MIX},
	{0,   "stereo",      GLO_INT,  set_frameflag, &frameflag, MPG123_FORCE_STEREO},
	{0,   "reopen",      GLO_INT,  0, &param.force_reopen, 1},
	{'g', "gain",        GLO_ARG | GLO_LONG, 0, &ai.gain,    0},
	{'r', "rate",        GLO_ARG | GLO_LONG, 0, &param.force_rate,  0},
	{0,   "8bit",        GLO_INT,  set_frameflag, &frameflag, MPG123_FORCE_8BIT},
	{0,   "headphones",  0,                  set_output_h, 0,0},
	{0,   "speaker",     0,                  set_output_s, 0,0},
	{0,   "lineout",     0,                  set_output_l, 0,0},
	{'o', "output",      GLO_ARG | GLO_CHAR, set_output, 0,  0},
#ifdef FLOATOUT
	{'f', "scale",       GLO_ARG | GLO_DOUBLE, 0, &param.outscale,   0},
#else
	{'f', "scale",       GLO_ARG | GLO_LONG, 0, &param.outscale,   0},
#endif
	{'n', "frames",      GLO_ARG | GLO_LONG, 0, &param.frame_number,  0},
	#ifdef HAVE_TERMIOS
	{'C', "control",     GLO_INT,  0, &param.term_ctrl, TRUE},
	#endif
	{'b', "buffer",      GLO_ARG | GLO_LONG, 0, &param.usebuffer,  0},
	{'R', "remote",      GLO_INT,  0, &param.remote, TRUE},
	{0,   "remote-err",  GLO_INT,  0, &param.remote_err, TRUE},
	{'d', "doublespeed", GLO_ARG | GLO_LONG, 0, &param.doublespeed, 0},
	{'h', "halfspeed",   GLO_ARG | GLO_LONG, 0, &param.halfspeed, 0},
	{'p', "proxy",       GLO_ARG | GLO_CHAR, 0, &htd.proxyurl,   0},
	{'@', "list",        GLO_ARG | GLO_CHAR, 0, &param.listname,   0},
	/* 'z' comes from the the german word 'zufall' (eng: random) */
	{'z', "shuffle",     GLO_INT,  0, &param.shuffle, 1},
	{'Z', "random",      GLO_INT,  0, &param.shuffle, 2},
	{'E', "equalizer",	 GLO_ARG | GLO_CHAR, 0, &equalfile,1},
	#ifdef HAVE_SETPRIORITY
	{0,   "aggressive",	 GLO_INT,  0, &param.aggressive, 2},
	#endif
	#ifdef OPT_3DNOW
#define SET_3DNOW 1
#define SET_I586  2
	{0,   "force-3dnow", GLO_CHAR,  0, &dnow, SET_3DNOW},
	{0,   "no-3dnow",    GLO_CHAR,  0, &dnow, SET_I586},
	{0,   "test-3dnow",  GLO_INT,  0, &param.test_cpu, TRUE},
	#endif
	#ifdef OPT_MULTI
	{0, "cpu", GLO_ARG | GLO_CHAR, 0, &param.cpu,  0},
	{0, "test-cpu",  GLO_INT,  0, &param.test_cpu, TRUE},
	{0, "list-cpu", GLO_INT,  0, &param.list_cpu , 1},
	#endif
	#if !defined(WIN32) && !defined(GENERIC)
	{'u', "auth",        GLO_ARG | GLO_CHAR, 0, &httpauth,   0},
	#endif
	#ifdef HAVE_SCHED_SETSCHEDULER
	/* check why this should be a long variable instead of int! */
	{'T', "realtime",    GLO_LONG,  0, &param.realtime, TRUE },
	#else
	{'T', "realtime",    0,  realtime_not_compiled, 0,           0 },    
	#endif
	{0, "title",         GLO_INT,  0, &param.xterm_title, TRUE },
	{'w', "wav",         GLO_ARG | GLO_CHAR, set_wav, 0 , 0 },
	{0, "cdr",           GLO_ARG | GLO_CHAR, set_cdr, 0 , 0 },
	{0, "au",            GLO_ARG | GLO_CHAR, set_au, 0 , 0 },
	#ifdef GAPLESS
	{0,   "gapless",	 GLO_INT,  set_frameflag, &frameflag, MPG123_GAPLESS},
	#endif
	{'?', "help",            0,  want_usage, 0,           0 },
	{0 , "longhelp" ,        0,  want_long_usage, 0,      0 },
	{0 , "version" ,         0,  give_version, 0,         0 },
	{'l', "listentry",       GLO_ARG | GLO_LONG, 0, &param.listentry, 0 },
	{0, "rva-mix",         GLO_INT,  0, &param.rva, 1 },
	{0, "rva-radio",         GLO_INT,  0, &param.rva, 1 },
	{0, "rva-album",         GLO_INT,  0, &param.rva, 2 },
	{0, "rva-audiophile",         GLO_INT,  0, &param.rva, 2 },
	{0, "no-icy-meta",      GLO_INT,  0, &param.talk_icy, 0 },
	{0, "long-tag",         GLO_INT,  0, &param.long_id3, 1 },
#ifdef FIFO
	{0, "fifo", GLO_ARG | GLO_CHAR, 0, &param.fifo,  0},
#endif
	{0, 0, 0, 0, 0, 0}
};

/*
 *   Change the playback sample rate.
 *   Consider that changing it after starting playback is not covered by gapless code!
 */
static void reset_audio(void)
{
#ifndef NOXFERMEM
	if (param.usebuffer) {
		/* wait until the buffer is empty,
		 * then tell the buffer process to
		 * change the sample rate.   [OF]
		 */
		while (xfermem_get_usedspace(buffermem)	> 0)
			if (xfermem_block(XF_WRITER, buffermem) == XF_CMD_TERMINATE) {
				intflag = TRUE;
				break;
			}
		buffermem->freeindex = -1;
		buffermem->readindex = 0; /* I know what I'm doing! ;-) */
		buffermem->freeindex = 0;
		if (intflag)
			return;
		buffermem->buf[0] = ai.rate; 
		buffermem->buf[1] = ai.channels; 
		buffermem->buf[2] = ai.format;
		buffer_reset();
	}
	else 
#endif
	if (param.outmode == DECODE_AUDIO) {
		/* audio_reset_parameters(&ai); */
		/*   close and re-open in order to flush
		 *   the device's internal buffer before
		 *   changing the sample rate.   [OF]
		 */
		audio_close (&ai);
		if (audio_open(&ai) < 0) {
			perror("audio");
			safe_exit(1);
		}
	}
}

/* 1 on success, 0 on failure */
int open_track(char *fname)
{
	int filept = -1;
	if(MPG123_OK != mpg123_param(mh, MPG123_ICY_INTERVAL, 0, 0))
	error1("Cannot (re)set ICY interval: %s", mpg123_strerror(mh));
	if(!fname) filept = STDIN_FILENO;
	else if (!strncmp(fname, "http://", 7)) /* http stream */
	{
		httpdata_reset(&htd);
		filept = http_open(fname, &htd);
		/* now check if we got sth. and if we got sth. good */
		if(    (filept >= 0) && (htd.content_type.p != NULL)
			  && strcmp(htd.content_type.p, "audio/mpeg") && strcmp(htd.content_type.p, "audio/x-mpeg") )
		{
			error1("Unknown mpeg MIME type %s - is it perhaps a playlist (use -@)?", htd.content_type.p == NULL ? "<nil>" : htd.content_type.p);
			error("If you know the stream is mpeg1/2 audio, then please report this as "PACKAGE_NAME" bug");
			return 0;
		}
		if(MPG123_OK != mpg123_param(mh, MPG123_ICY_INTERVAL, htd.icy_interval, 0))
		error1("Cannot set ICY interval: %s", mpg123_strerror(mh));
		if(param.verbose > 1) fprintf(stderr, "Info: ICY interval %li\n", (long)htd.icy_interval);
	}
	debug("OK... going to finally open.");
	/* Now hook up the decoder on the opened stream or the file. */
	if(filept > -1)
	{
		if(mpg123_open_fd(mh, filept) != MPG123_OK)
		{
			error2("Cannot open fd %i: %s", filept, mpg123_strerror(mh));
			return 0;
		}
	}
	else if(mpg123_open(mh, fname) != MPG123_OK)
	{
		error2("Cannot open %s: %s", fname, mpg123_strerror(mh));
		return 0;
	}
	debug("Track successfully opened.");
	return 1;
}

/* for symmetry */
void close_track(void)
{
	mpg123_close(mh);
}

/* return 1 on success, 0 on failure */
int play_frame(void)
{
	unsigned char *audio;
	size_t bytes;
	/* The first call will not decode anything but return MPG123_NEW_FORMAT! */
	int mc = mpg123_decode_frame(mh, &framenum, &audio, &bytes);
	/* Play what is there to play (starting with second decode_frame call!) */
	if(bytes)
	{
		if(param.frame_number > -1) --frames_left;
		if(framenum == param.start_frame && !param.quiet)
		{
			if(param.verbose) print_header(mh);
			else print_header_compact(mh);
		}
#ifndef NOXFERMEM
		if(param.usebuffer)
		{ /* We decoded directly into the buffer's buffer. */
			if(!intflag)
			{
				buffermem->freeindex =
					(buffermem->freeindex + bytes) % buffermem->size;
				if (buffermem->wakeme[XF_READER])
					xfermem_putcmd(buffermem->fd[XF_WRITER], XF_CMD_WAKEUP_INFO);
			}
			mpg123_replace_buffer(mh, (unsigned char *) (buffermem->data + buffermem->freeindex), bufferblock);
			while (xfermem_get_freespace(buffermem) < bufferblock)
				if (xfermem_block(XF_WRITER, buffermem) == XF_CMD_TERMINATE)
				{
					intflag = TRUE;
					break;
				}
			if(intflag) return 1;
		}
		else
#endif
		/* Normal flushing of data. */
		audio_flush(param.outmode, audio, bytes, &ai);
		if(param.checkrange)
		{
			long clip = mpg123_clip(mh);
			if(clip > 0) fprintf(stderr,"%ld samples clipped\n", clip);
		}
	}
	/* Special actions and errors. */
	if(mc != MPG123_OK)
	{
		if(mc == MPG123_ERR || mc == MPG123_DONE)
		{
			if(mc == MPG123_ERR) error1("...in decoding next frame: %s", mpg123_strerror(mh));
			return 0;
		}
		if(mc == MPG123_NO_SPACE)
		{
			error("I have not enough output space? I didn't plan for this.");
			return 0;
		}
		if(mc == MPG123_NEW_FORMAT)
		{
			mpg123_getformat(mh, &ai.rate, &ai.channels, &ai.format);
			if(init_output()) reset_audio();
		}
	}
	return 1;
}

int main(int argc, char *argv[])
{
	int result;
	long parr;
	char *fname;
	mpg123_pars *mp;
#if !defined(WIN32) && !defined(GENERIC)
	struct timeval start_time, now;
	unsigned long secdiff;
#endif
	httpdata_init(&htd);
	result = mpg123_init();
	if(result != MPG123_OK)
	{
		error1("Cannot initialize mpg123 library: %s", mpg123_plain_strerror(result));
		safe_exit(77);
	}
	mp = mpg123_new_pars(&result);
	if(mp == NULL)
	{
		error1("Crap! Cannot get mpg123 parameters: %s", mpg123_plain_strerror(result));
		safe_exit(77);
	}

	/* get default values */
	mpg123_getpar(mp, MPG123_DOWN_SAMPLE, &parr, NULL);
	param.down_sample = (int) parr;
	mpg123_getpar(mp, MPG123_RVA, &param.rva, NULL);
	mpg123_getpar(mp, MPG123_DOWNSPEED, &param.halfspeed, NULL);
	mpg123_getpar(mp, MPG123_UPSPEED, &param.doublespeed, NULL);
#ifdef FLOATOUT
	mpg123_getpar(mp, MPG123_OUTSCALE, NULL, &param.outscale);
#else
	mpg123_getpar(mp, MPG123_OUTSCALE, &param.outscale, NULL);
#endif
	mpg123_getpar(mp, MPG123_FLAGS, &parr, NULL);
	param.flags = (int) parr;
	bufferblock = mpg123_safe_buffer();

#ifdef OS2
        _wildcard(&argc,&argv);
#endif

	(prgName = strrchr(argv[0], '/')) ? prgName++ : (prgName = argv[0]);

	audio_info_struct_init(&ai);

	while ((result = getlopt(argc, argv, opts)))
	switch (result) {
		case GLO_UNKNOWN:
			fprintf (stderr, "%s: Unknown option \"%s\".\n", 
				prgName, loptarg);
			usage(1);
		case GLO_NOARG:
			fprintf (stderr, "%s: Missing argument for option \"%s\".\n",
				prgName, loptarg);
			usage(1);
	}

	if(param.list_cpu)
	{
		char **all_dec = mpg123_decoders();
		printf("Builtin decoders:");
		while(*all_dec != NULL){ printf(" %s", *all_dec); ++all_dec; }
		printf("\n");
		safe_exit(0);
	}
	if(param.test_cpu)
	{
		char **all_dec = mpg123_supported_decoders();
		printf("Supported decoders:");
		while(*all_dec != NULL){ printf(" %s", *all_dec); ++all_dec; }
		printf("\n");
		safe_exit(0);
	}

	if (loptind >= argc && !param.listname && !param.remote) usage(1);

#if !defined(WIN32) && !defined(GENERIC)
	if (param.remote)
	{
		param.verbose = 0;
		param.quiet = 1;
		param.flags |= MPG123_QUIET;
	}
#endif

	/* Set the frame parameters from command line options */
	if(param.quiet) param.flags |= MPG123_QUIET;
#ifdef OPT_3DNOW
	if(dnow != 0) param.cpu = (dnow == SET_3DNOW) ? "3dnow" : "i586";
#endif
	if(param.cpu != NULL && (!strcmp(param.cpu, "auto") || !strcmp(param.cpu, ""))) param.cpu = NULL;
	if(!(  MPG123_OK == (result = mpg123_par(mp, MPG123_VERBOSE, param.verbose, 0))
	    && MPG123_OK == (result = mpg123_par(mp, MPG123_FLAGS, param.flags, 0))
	    && MPG123_OK == (result = mpg123_par(mp, MPG123_DOWN_SAMPLE, param.down_sample, 0))
	    && MPG123_OK == (result = mpg123_par(mp, MPG123_RVA, param.rva, 0))
	    && MPG123_OK == (result = mpg123_par(mp, MPG123_DOWNSPEED, param.halfspeed, 0))
	    && MPG123_OK == (result = mpg123_par(mp, MPG123_UPSPEED, param.doublespeed, 0))
	    && MPG123_OK == (result = mpg123_par(mp, MPG123_ICY_INTERVAL, 0, 0))
#ifdef FLOATOUT
	    && MPG123_OK == (result = mpg123_par(mp, MPG123_OUTSCALE, 0, param.outscale))
#else
	    && MPG123_OK == (result = mpg123_par(mp, MPG123_OUTSCALE, param.outscale, 0))
#endif
			))
	{
		error1("Cannot set library parameters: %s", mpg123_plain_strerror(result));
		safe_exit(45);
	}
	if (!(param.listentry < 0) && !param.quiet) print_title(stderr); /* do not pollute stdout! */

	if(param.force_rate && param.down_sample)
	{
		error("Down sampling and fixed rate options not allowed together!");
		safe_exit(1);
	}

	/* Now actually get an mpg123_handle. */
	mh = mpg123_parnew(mp, NULL, &result);
	if(mh == NULL)
	{
		error1("Crap! Cannot get a mpg123 handle: %s", mpg123_plain_strerror(result));
		safe_exit(77);
	}
	mpg123_delete_pars(mp); /* Don't need the parameters anymore ,they're in the handle now. */
	if(param.cpu != NULL && (MPG123_OK != mpg123_decoder(mh, param.cpu)))
	{
		error1("Unable to set decoder: %s", mpg123_strerror(mh));
		safe_exit(46);
	}
	audio_capabilities(&ai, mh); /* Query audio output parameters, store in mpg123 handle. */

	if(equalfile != NULL)
	{ /* tst; ThOr: not TRUE or FALSE: allocated or not... */
		FILE *fe;
		int i;
		fe = fopen(equalfile,"r");
		if(fe) {
			char line[256];
			for(i=0;i<32;i++) {
				float e1,e0; /* %f -> float! */
				line[0]=0;
				fgets(line,255,fe);
				if(line[0]=='#')
					continue;
				sscanf(line,"%f %f",&e0,&e1);
				mpg123_eq(mh, MPG123_LEFT,  i, e0);
				mpg123_eq(mh, MPG123_RIGHT, i, e1);
			}
			fclose(fe);
		}
		else
			fprintf(stderr,"Can't open equalizer file '%s'\n",equalfile);
	}

#ifdef HAVE_SETPRIORITY
	if(param.aggressive) { /* tst */
		int mypid = getpid();
		setpriority(PRIO_PROCESS,mypid,-20);
	}
#endif

#ifdef HAVE_SCHED_SETSCHEDULER
	if (param.realtime) {  /* Get real-time priority */
	  struct sched_param sp;
	  fprintf(stderr,"Getting real-time priority\n");
	  memset(&sp, 0, sizeof(struct sched_param));
	  sp.sched_priority = sched_get_priority_min(SCHED_FIFO);
	  if (sched_setscheduler(0, SCHED_RR, &sp) == -1)
	    fprintf(stderr,"Can't get real-time priority\n");
	}
#endif

	if(!param.remote) prepare_playlist(argc, argv);

#if !defined(WIN32) && !defined(GENERIC)
	/* This ctrl+c for title skip only when not in some control mode */
	if
	(
		!param.remote 
		#ifdef HAVE_TERMIOS
		&& !param.term_ctrl
		#endif
	)
	catchsignal (SIGINT, catch_interrupt);

	if(param.remote) {
		int ret;
		ret = control_generic(mh);
		safe_exit(ret);
	}
#endif

	while ((fname = get_next_file()))
	{
		char *dirname, *filename;
		frames_left = param.frame_number;
		debug1("Going to play %s", fname != NULL ? fname : "standard input");

		if(!open_track(fname)) continue;

		framenum = mpg123_seek_frame(mh, param.start_frame, SEEK_SET);
		if(framenum < 0)
		{
			error1("Initial seek failed: %s", mpg123_strerror(mh));
			continue;
		}

		if (!param.quiet) {
			if (split_dir_file(fname ? fname : "standard input",
				&dirname, &filename))
				fprintf(stderr, "\nDirectory: %s", dirname);
			fprintf(stderr, "\nPlaying MPEG stream %lu of %lu: %s ...\n", (unsigned long)pl.pos, (unsigned long)pl.fill, filename);

#if !defined(GENERIC)
{
	const char *term_type;
	term_type = getenv("TERM");
	if (term_type && param.xterm_title &&
	    (!strncmp(term_type,"xterm",5) || !strncmp(term_type,"rxvt",4)))
	{
		fprintf(stderr, "\033]0;%s\007", filename);
	}
}
#endif

		}

#if !defined(WIN32) && !defined(GENERIC)
#ifdef HAVE_TERMIOS
		if(!param.term_ctrl)
#endif
			gettimeofday (&start_time, NULL);
#endif

#ifdef HAVE_TERMIOS
		debug1("param.term_ctrl: %i", param.term_ctrl);
		if(param.term_ctrl)
			term_init();
#endif
#ifdef HAVE_TERMIOS /* I am not sure if this is right here anymore!... what with intflag? */
tc_hack:
#endif
		while(!intflag)
		{
			int meta;
			if(param.frame_number > -1)
			{
				debug1("frames left: %li", (long) frames_left);
				if(!frames_left) break;
			}
			if(!play_frame()) break;
			if(!param.quiet)
			{
				meta = mpg123_meta_check(mh);
				if(meta & (MPG123_NEW_ID3|MPG123_NEW_ICY))
				{
					char *icy;
					if(meta & MPG123_NEW_ID3) print_id3_tag(mh, param.long_id3, stderr);
					if(meta & MPG123_NEW_ICY && MPG123_OK == mpg123_icy(mh, &icy))
					fprintf(stderr, "\nICY-META: %s\n", icy);
				}
			}
			if(param.verbose)
			{
#ifndef NOXFERMEM
				if (param.verbose > 1 || !(framenum & 0x7))
					print_stat(mh,0,xfermem_get_usedspace(buffermem)); 
				if(param.verbose > 2 && param.usebuffer)
					fprintf(stderr,"[%08x %08x]",buffermem->readindex,buffermem->freeindex);
#else
				if(param.verbose > 1 || !(framenum & 0x7))	print_stat(mh,0,0);
#endif
			}
#ifdef HAVE_TERMIOS
			if(!param.term_ctrl) continue;
			else
			{
				off_t offset;
				if((offset=term_control(mh,&ai)))
				{
					if((offset = mpg123_seek_frame(mh, offset, SEEK_CUR)) >= 0)
					{
						if(param.usebuffer)	buffer_resync();
						debug1("seeked to %li", offset);
					} else error1("seek failed: %s!", mpg123_strerror(mh));
				}
			}
#endif
		}

#ifndef NOXFERMEM
	if(param.usebuffer) {
		int s;
		while ((s = xfermem_get_usedspace(buffermem)))
		{
			struct timeval wait170 = {0, 170000};
			buffer_ignore_lowmem();
			if(param.verbose) print_stat(mh,0,s);
#ifdef HAVE_TERMIOS
			if(param.term_ctrl)
			{
				off_t offset;
				if((offset=term_control(mh,&ai)))
				{
					if((offset = mpg123_seek_frame(mh, offset, SEEK_CUR)) >= 0)
					/*	&& read_frame(&fr) == 1 */
					{
						if(param.usebuffer)	buffer_resync();
						debug1("seeked to %li", offset);
						goto tc_hack;	/* Doh! Gag me with a spoon! */
					} else error1("seek failed: %s!", mpg123_strerror(mh));
				}
			}
#endif
			select(0, NULL, NULL, NULL, &wait170);
		}
	}
#endif
	if(param.verbose) print_stat(mh,0,xfermem_get_usedspace(buffermem)); 
#ifdef HAVE_TERMIOS
	if(param.term_ctrl) term_restore();
#endif

	if(!param.quiet)
	{
		double secs;
		mpg123_position(mh, 0, 0, NULL, NULL, &secs, NULL);
		fprintf(stderr,"\n[%d:%02d] Decoding of %s finished.\n", (int)(secs / 60), ((int)secs) % 60, filename);
	}

	mpg123_close(mh);

	if (intflag)
	{
/* 
 * When HAVE_TERMIOS is defined, there is 'q' to terminate a list of songs, so
 * no pressing need to keep up this first second SIGINT hack that was too
 * often mistaken as a bug. [dk]
 * ThOr: Yep, I deactivated the Ctrl+C hack for active control modes.
 */
#if !defined(WIN32) && !defined(GENERIC)
#ifdef HAVE_TERMIOS
	if(!param.term_ctrl)
#endif
        {
		gettimeofday (&now, NULL);
        	secdiff = (now.tv_sec - start_time.tv_sec) * 1000;
        	if (now.tv_usec >= start_time.tv_usec)
          		secdiff += (now.tv_usec - start_time.tv_usec) / 1000;
        	else
          		secdiff -= (start_time.tv_usec - now.tv_usec) / 1000;
        	if (secdiff < 1000)
          		break;
	}
#endif
        intflag = FALSE;

#ifndef NOXFERMEM
        if(param.usebuffer) buffer_resync();
#endif
      }
    } /* end of loop over input files */
#ifndef NOXFERMEM
    if (param.usebuffer) {
      buffer_end();
      xfermem_done_writer (buffermem);
      waitpid (buffer_pid, NULL, 0);
      xfermem_done (buffermem);
    }
#endif
    switch(param.outmode)
		{
      case DECODE_AUDIO:
        audio_close(&ai);
        break;
      case DECODE_WAV:
        wav_close();
        break;
      case DECODE_AU:
        au_close();
        break;
      case DECODE_CDR:
        cdr_close();
        break;
    }
   	if(!param.remote) free_playlist();
	mpg123_delete(mh);
	mpg123_exit();
	httpdata_reset(&htd);
	return 0;
}

static void print_title(FILE *o)
{
	fprintf(o, "High Performance MPEG 1.0/2.0/2.5 Audio Player for Layers 1, 2 and 3\n");
	fprintf(o, "\tversion %s; written and copyright by Michael Hipp and others\n", PACKAGE_VERSION);
	fprintf(o, "\tfree software (LGPL/GPL) without any warranty but with best wishes\n");
}

static void usage(int err)  /* print syntax & exit */
{
	FILE* o = stdout;
	if(err)
	{
		o = stderr; 
		fprintf(o, "You made some mistake in program usage... let me briefly remind you:\n\n");
	}
	print_title(o);
	fprintf(o,"\nusage: %s [option(s)] [file(s) | URL(s) | -]\n", prgName);
	fprintf(o,"supported options [defaults in brackets]:\n");
	fprintf(o,"   -v    increase verbosity level       -q    quiet (don't print title)\n");
	fprintf(o,"   -t    testmode (no output)           -s    write to stdout\n");
	fprintf(o,"   -w <filename> write Output as WAV file\n");
	fprintf(o,"   -k n  skip first n frames [0]        -n n  decode only n frames [all]\n");
	fprintf(o,"   -c    check range violations         -y    DISABLE resync on errors\n");
	fprintf(o,"   -b n  output buffer: n Kbytes [0]    -f n  change scalefactor [%g]\n", (double)param.outscale);
	fprintf(o,"   -r n  set/force samplerate [auto]    -g n  set audio hardware output gain\n");
	fprintf(o,"   -os,-ol,-oh  output to built-in speaker,line-out connector,headphones\n");
	#ifdef NAS
	fprintf(o,"                                        -a d  set NAS server\n");
	#elif defined(SGI)
	fprintf(o,"                                        -a [1..4] set RAD device\n");
	#else
	fprintf(o,"                                        -a d  set audio device\n");
	#endif
	fprintf(o,"   -2    downsample 1:2 (22 kHz)        -4    downsample 1:4 (11 kHz)\n");
	fprintf(o,"   -d n  play every n'th frame only     -h n  play every frame n times\n");
	fprintf(o,"   -0    decode channel 0 (left) only   -1    decode channel 1 (right) only\n");
	fprintf(o,"   -m    mix both channels (mono)       -p p  use HTTP proxy p [$HTTP_PROXY]\n");
	#ifdef HAVE_SCHED_SETSCHEDULER
	fprintf(o,"   -@ f  read filenames/URLs from f     -T get realtime priority\n");
	#else
	fprintf(o,"   -@ f  read filenames/URLs from f\n");
	#endif
	fprintf(o,"   -z    shuffle play (with wildcards)  -Z    random play\n");
	fprintf(o,"   -u a  HTTP authentication string     -E f  Equalizer, data from file\n");
	#ifdef GAPLESS
	fprintf(o,"   -C    enable control keys            --gapless  skip junk/padding in some mp3s\n");
	#else
	fprintf(o,"   -C    enable control keys\n");
	#endif
	fprintf(o,"   -?    this help                      --version  print name + version\n");
	fprintf(o,"See the manpage %s(1) or call %s with --longhelp for more parameters and information.\n", prgName,prgName);
	safe_exit(err);
}

static void want_usage(char* arg)
{
	usage(0);
}

static void long_usage(int err)
{
	FILE* o = stdout;
	if(err)
	{
  	o = stderr; 
  	fprintf(o, "You made some mistake in program usage... let me remind you:\n\n");
	}
	print_title(o);
	fprintf(o,"\nusage: %s [option(s)] [file(s) | URL(s) | -]\n", prgName);

	fprintf(o,"\ninput options\n\n");
	fprintf(o," -k <n> --skip <n>         skip n frames at beginning\n");
	fprintf(o," -n     --frames <n>       play only <n> frames of every stream\n");
	fprintf(o," -y     --resync           DISABLES resync on error\n");
	fprintf(o," -p <f> --proxy <f>        set WWW proxy\n");
	fprintf(o," -u     --auth             set auth values for HTTP access\n");
	fprintf(o," -@ <f> --list <f>         play songs in playlist <f> (plain list, m3u, pls (shoutcast))\n");
	fprintf(o," -l <n> --listentry <n>    play nth title in playlist; show whole playlist for n < 0\n");
	fprintf(o," -z     --shuffle          shuffle song-list before playing\n");
	fprintf(o," -Z     --random           full random play\n");
	fprintf(o,"        --no-icy-meta      Do not accept ICY meta data\n");
	fprintf(o,"\noutput/processing options\n\n");
	fprintf(o," -a <d> --audiodevice <d>  select audio device\n");
	fprintf(o," -s     --stdout           write raw audio to stdout (host native format)\n");
	fprintf(o," -S     --STDOUT           play AND output stream (not implemented yet)\n");
	fprintf(o," -w <f> --wav <f>          write samples as WAV file in <f> (- is stdout)\n");
	fprintf(o,"        --au <f>           write samples as Sun AU file in <f> (- is stdout)\n");
	fprintf(o,"        --cdr <f>          write samples as CDR file in <f> (- is stdout)\n");
	fprintf(o,"        --reopen           force close/open on audiodevice\n");
	#ifdef OPT_MULTI
	fprintf(o,"        --cpu <string>     set cpu optimization\n");
	fprintf(o,"        --test-cpu         list optmizations possible with cpu and exit\n");
	fprintf(o,"        --list-cpu         list builtin optimizations and exit\n");
	#endif
	#ifdef OPT_3DNOW
	fprintf(o,"        --test-3dnow       display result of 3DNow! autodetect and exit (obsoleted by --cpu)\n");
	fprintf(o,"        --force-3dnow      force use of 3DNow! optimized routine (obsoleted by --test-cpu)\n");
	fprintf(o,"        --no-3dnow         force use of floating-pointer routine (obsoleted by --cpu)\n");
	#endif
	fprintf(o," -g     --gain             set audio hardware output gain\n");
	fprintf(o," -f <n> --scale <n>        scale output samples (soft gain, default=%g)\n", (double)param.outscale);
	fprintf(o,"        --rva-mix,\n");
	fprintf(o,"        --rva-radio        use RVA2/ReplayGain values for mix/radio mode\n");
	fprintf(o,"        --rva-album,\n");
	fprintf(o,"        --rva-audiophile   use RVA2/ReplayGain values for album/audiophile mode\n");
	fprintf(o," -0     --left --single0   play only left channel\n");
	fprintf(o," -1     --right --single1  play only right channel\n");
	fprintf(o," -m     --mono --mix       mix stereo to mono\n");
	fprintf(o,"        --stereo           duplicate mono channel\n");
	fprintf(o," -r     --rate             force a specific audio output rate\n");
	fprintf(o," -2     --2to1             2:1 downsampling\n");
	fprintf(o," -4     --4to1             4:1 downsampling\n");
	fprintf(o,"        --8bit             force 8 bit output\n");
	fprintf(o," -d n   --doublespeed n    play only every nth frame\n");
	fprintf(o," -h n   --halfspeed   n    play every frame n times\n");
	fprintf(o,"        --equalizer        exp.: scales freq. bands acrd. to 'equalizer.dat'\n");
	#ifdef GAPLESS
	fprintf(o,"        --gapless          remove padding/junk added by encoder/decoder\n");
	#endif
	fprintf(o,"                           (experimental, needs Lame tag, layer 3 only)\n");
	fprintf(o," -o h   --headphones       (aix/hp/sun) output on headphones\n");
	fprintf(o," -o s   --speaker          (aix/hp/sun) output on speaker\n");
	fprintf(o," -o l   --lineout          (aix/hp/sun) output to lineout\n");
	fprintf(o," -b <n> --buffer <n>       set play buffer (\"output cache\")\n");

	fprintf(o,"\nmisc options\n\n");
	fprintf(o," -t     --test             only decode, no output (benchmark)\n");
	fprintf(o," -c     --check            count and display clipped samples\n");
	fprintf(o," -v[*]  --verbose          increase verboselevel\n");
	fprintf(o," -q     --quiet            quiet mode\n");
	#ifdef HAVE_TERMIOS
	fprintf(o," -C     --control          enable terminal control keys\n");
	#endif
	#ifndef GENERIG
	fprintf(o,"        --title            set xterm/rxvt title to filename\n");
	#endif
	fprintf(o,"        --long-tag         spacy id3 display with every item on a separate line\n");
	fprintf(o," -R     --remote           generic remote interface\n");
	fprintf(o,"        --remote-err       force use of stderr for generic remote interface\n");
#ifdef FIFO
	fprintf(o,"        --fifo <path>      open a FIFO at <path> for commands instead of stdin\n");
#endif
	#ifdef HAVE_SETPRIORITY
	fprintf(o,"        --aggressive       tries to get higher priority (nice)\n");
	#endif
	#ifdef HAVE_SCHED_SETSCHEDULER
	fprintf(o," -T     --realtime         tries to get realtime priority\n");
	#endif
	fprintf(o," -?     --help             give compact help\n");
	fprintf(o,"        --longhelp         give this long help listing\n");
	fprintf(o,"        --version          give name / version string\n");

	fprintf(o,"\nSee the manpage %s(1) for more information.\n", prgName);
	safe_exit(err);
}

static void want_long_usage(char* arg)
{
	long_usage(0);
}

static void give_version(char* arg)
{
	fprintf(stdout, PACKAGE_NAME" "PACKAGE_VERSION"\n");
	safe_exit(0);
}