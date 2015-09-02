/*
	out123: simple program to stream data to an audio output device

	copyright 1995-2015 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Thomas Orgis (extracted from mpg123.c)

	This rips out anything dealing with MPEG decoding and playlist handling,
	reducing the functionality to what shall become part of libout123: Simple
	means to get mono/stereo audio to various output devices on various
	platforms. Since this is rather redundant to other approaches (portaudio,
	etc.), the justification stems from two points:

	1. It's code home-grown for mpg123, defining a feature set without resorting
	another build dependency. Throwing the code away would reduce what the mpg123
	code base offers by itself.

	2. It's feature set is a commonly-needed subset of what a full audio
	framework provides. It's what simple playback of mono/stereo data needs.
	Therefore, it has a chance to stay (be?) simple to use, explicitly hiding
	complexity.

	TODO: Decide if the buffer process should be part of it. Might be a good
	idea. It's specific to output and is something tricky to get going yourself.
*/

#define ME "main"
#include "mpg123app.h"
#include "out123.h"

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include <errno.h>
#include <string.h>
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

#include "debug.h"

static void usage(int err);
static void want_usage(char* arg);
static void long_usage(int err);
static void want_long_usage(char* arg);
static void print_title(FILE* o);
static void give_version(char* arg);

static FILE* input = NULL;
size_t pcmblock = 1152; /* samples (pcm frames) we treat en bloc */
/* To be set after settling format. */
size_t pcmframe = 0;
unsigned char *audio = NULL;


struct parameter param = { 
  FALSE , /* aggressiv */
  FALSE , /* shuffle */
  FALSE , /* remote */
  FALSE , /* remote to stderr */
  FALSE , /* silent operation */
  FALSE , /* xterm title on/off */
  0 ,     /* second level buffer size */
  0 ,     /* verbose level */
  DEFAULT_OUTPUT_MODULE,	/* output module */
  NULL,   /* output device */
  0,      /* destination (headphones, ...) */
#ifdef HAVE_TERMIOS
  FALSE , /* term control */
  NULL,
  NULL,
#endif
  FALSE , /* checkrange */
  0 ,	  /* force_reopen, always (re)opens audio device for next song */
  /* test_cpu flag is valid for multi and 3dnow.. even if 3dnow is built alone; ensure it appears only once */
  FALSE , /* normal operation */
  FALSE,  /* try to run process in 'realtime mode' */
#ifdef HAVE_WINDOWS_H 
  0, /* win32 process priority */
#endif
	0, /* default is to play all titles in playlist */
	NULL, /* no playlist per default */
	0 /* condensed id3 per default */
	,0 /* list_cpu */
	,NULL /* cpu */ 
#ifdef FIFO
	,NULL
#endif
	,0 /* timeout */
	,1 /* loop */
	,0 /* delay */
	,0 /* index */
	/* Parameters for mpg123 handle, defaults are queried from library! */
	,0 /* down_sample */
	,0 /* rva */
	,0 /* halfspeed */
	,0 /* doublespeed */
	,0 /* start_frame */
	,-1 /* frame_number */
	,0 /* outscale */
	,0 /* flags */
	,44100 /* force_rate */
	,1 /* ICY */
	,1024 /* resync_limit */
	,0 /* smooth */
	,0.0 /* pitch */
	,0 /* appflags */
	,NULL /* proxyurl */
	,0 /* keep_open */
	,0 /* force_utf8 */
	,INDEX_SIZE
	,"s16" /* force_encoding */
	,0.2 /* preload */
	,-1 /* preframes */
	,-1 /* gain */
	,NULL /* stream dump file */
	,0 /* ICY interval */
};

audio_output_t *ao = NULL;
char *prgName = NULL;
/* ThOr: pointers are not TRUE or FALSE */
char *equalfile = NULL;
int fresh = TRUE;

size_t bufferblock = 4096;

int OutputDescriptor;

char *fullprogname = NULL; /* Copy of argv[0]. */
char *binpath; /* Path to myself. */

/* File-global storage of command line arguments.
   They may be needed for cleanup after charset conversion. */
static char **argv = NULL;
static int    argc = 0;

void safe_exit(int code)
{
	char *dummy, *dammy;

	out123_del(ao);
#ifdef WANT_WIN32_UNICODE
	win32_cmdline_free(argc, argv); /* This handles the premature argv == NULL, too. */
#endif
	/* It's ugly... but let's just fix this still-reachable memory chunk of static char*. */
	split_dir_file("", &dummy, &dammy);
	if(fullprogname) free(fullprogname);
	if(audio) free(audio);
	exit(code);
}

static void check_fatal(int code)
{
	if(code) safe_exit(code);
}

static void check_fatal_output(int code)
{
	if(code)
	{
		if(!param.quiet)
			error2( "out123 error %i: %s"
			,	out123_errcode(ao), out123_strerror(ao) );
		safe_exit(code);
	}
}

static void set_output_module( char *arg )
{
	unsigned int i;
		
	/* Search for a colon and set the device if found */
	for(i=0; i< strlen( arg ); i++) {
		if (arg[i] == ':') {
			arg[i] = 0;
			param.output_device = &arg[i+1];
			debug1("Setting output device: %s", param.output_device);
			break;
		}	
	}
	/* Set the output module */
	param.output_module = arg;
	debug1("Setting output module: %s", param.output_module );
}

static void set_output_flag(int flag)
{
  if(param.output_flags <= 0) param.output_flags = flag;
  else param.output_flags |= flag;
}

static void set_output_h(char *a)
{
	set_output_flag(AUDIO_OUT_HEADPHONES);
}

static void set_output_s(char *a)
{
	set_output_flag(AUDIO_OUT_INTERNAL_SPEAKER);
}

static void set_output_l(char *a)
{
	set_output_flag(AUDIO_OUT_LINE_OUT);
}

static void set_output(char *arg)
{
	/* If single letter, it's the legacy output switch for AIX/HP/Sun.
	   If longer, it's module[:device] . If zero length, it's rubbish. */
	if(strlen(arg) <= 1) switch(arg[0])
	{
		case 'h': set_output_h(arg); break;
		case 's': set_output_s(arg); break;
		case 'l': set_output_l(arg); break;
		default:
			error1("\"%s\" is no valid output", arg);
			safe_exit(1);
	}
	else set_output_module(arg);
}

static void set_verbose (char *arg)
{
    param.verbose++;
}

static void set_quiet (char *arg)
{
	param.verbose=0;
	param.quiet=TRUE;
}

static void set_out_wav(char *arg)
{
	param.output_module = "wav";
	param.output_device = arg;
}

void set_out_cdr(char *arg)
{
	param.output_module = "cdr";
	param.output_device = arg;
}

void set_out_au(char *arg)
{
	param.output_module = "au";
	param.output_device = arg;
}

void set_out_test(char *arg)
{
	param.output_module = "test";
	param.output_device = NULL;
}

static void set_out_file(char *arg)
{
	param.output_module = "raw";
	param.output_device = arg;
}

static void set_out_stdout(char *arg)
{
	param.output_module = "raw";
	param.output_device = NULL;
}

static void set_out_stdout1(char *arg)
{
	param.output_module = "raw";
	param.output_device = NULL;
}

#if !defined (HAVE_SCHED_SETSCHEDULER) && !defined (HAVE_WINDOWS_H)
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
/* Disabled while not used to make pedantic gcc happy
static void unset_frameflag(char *arg)
{
	param.flags &= ~frameflag;
} */

static void list_output_modules(char *arg)
{
	char **names = NULL;
	char **descr = NULL;
	int count = -1;
	audio_output_t *lao;

	if((lao=out123_new()))
	{
		out123_param(lao, OUT123_VERBOSE, param.verbose, 0.);
		out123_param(lao, OUT123_FLAGS, OUT123_QUIET, 0.);
		if((count=out123_drivers(lao, &names, &descr)) >= 0)
		{
			int i;
			for(i=0; i<count; ++i)
				printf( "%-15s\t%s\n", names[i], descr[i] );
			free(names);
			free(descr);
		}
		out123_del(lao);
	}
	else if(!param.quiet)
		error("Failed to create an out123 handle.");
	exit(count >= 0 ? 0 : 1);
}

/*static int appflag;*/ /* still ugly, but works */
/*static void set_appflag(char *arg)
{
	param.appflags |= appflag;
}
static void unset_appflag(char *arg)
{
	param.appflags &= ~appflag;
} */

/* Please note: GLO_NUM expects point to LONG! */
/* ThOr:
 *  Yeah, and despite that numerous addresses to int variables were 
passed.
 *  That's not good on my Alpha machine with int=32bit and long=64bit!
 *  Introduced GLO_INT and GLO_LONG as different bits to make that clear.
 *  GLO_NUM no longer exists.
 */
topt opts[] = {
	{'t', "test",        GLO_INT,  set_out_test, NULL, 0},
	{'s', "stdout",      GLO_INT,  set_out_stdout,  NULL, 0},
	{'S', "STDOUT",      GLO_INT,  set_out_stdout1, NULL, 0},
	{'O', "outfile",     GLO_ARG | GLO_CHAR, set_out_file, NULL, 0},
	{'v', "verbose",     0,        set_verbose, 0,           0},
	{'q', "quiet",       0,        set_quiet,   0,           0},
	{'m',  "mono",        GLO_INT,  set_frameflag, &frameflag, MPG123_MONO_MIX},
	{0,   "stereo",      GLO_INT,  set_frameflag, &frameflag, MPG123_FORCE_STEREO},
	{'r', "rate",        GLO_ARG | GLO_LONG, 0, &param.force_rate,  0},
	{0,   "headphones",  0,                  set_output_h, 0,0},
	{0,   "speaker",     0,                  set_output_s, 0,0},
	{0,   "lineout",     0,                  set_output_l, 0,0},
	{'o', "output",      GLO_ARG | GLO_CHAR, set_output, 0,  0},
	{0,   "list-modules",0,       list_output_modules, NULL,  0}, 
	{'a', "audiodevice", GLO_ARG | GLO_CHAR, 0, &param.output_device,  0},
#ifndef NOXFERMEM
	{'b', "buffer",      GLO_ARG | GLO_LONG, 0, &param.usebuffer,  0},
	{0, "preload", GLO_ARG|GLO_DOUBLE, 0, &param.preload, 0},
#endif
#ifdef HAVE_SETPRIORITY
	{0,   "aggressive",	 GLO_INT,  0, &param.aggressive, 2},
#endif
#if defined (HAVE_SCHED_SETSCHEDULER) || defined (HAVE_WINDOWS_H)
	/* check why this should be a long variable instead of int! */
	{'T', "realtime",    GLO_LONG,  0, &param.realtime, TRUE },
#else
	{'T', "realtime",    0,  realtime_not_compiled, 0,           0 },    
#endif
#ifdef HAVE_WINDOWS_H
	{0, "priority", GLO_ARG | GLO_INT, 0, &param.w32_priority, 0},
#endif
	{'w', "wav",         GLO_ARG | GLO_CHAR, set_out_wav, 0, 0 },
	{0, "cdr",           GLO_ARG | GLO_CHAR, set_out_cdr, 0, 0 },
	{0, "au",            GLO_ARG | GLO_CHAR, set_out_au, 0, 0 },
	{'?', "help",            0,  want_usage, 0,           0 },
	{0 , "longhelp" ,        0,  want_long_usage, 0,      0 },
	{0 , "version" ,         0,  give_version, 0,         0 },
	{'e', "encoding", GLO_ARG|GLO_CHAR, 0, &param.force_encoding, 0},
	{0, 0, 0, 0, 0, 0}
};

/* return 1 on success, 0 on failure */
int play_frame(void)
{
	size_t got_samples;
	got_samples = fread(audio, pcmframe, pcmblock, input);
	/* Play what is there to play (starting with second decode_frame call!) */
	if(got_samples)
	{
		size_t got_bytes = pcmframe * got_samples;
		if(out123_play(ao, audio, got_bytes) < (int)got_bytes)
		{
			if(!param.quiet)
			{
				error2( "out123 error %i: %s"
				,	out123_errcode(ao), out123_strerror(ao) );
			}
			safe_exit(133);
		}
		return 1;
	}
	else return 0;
}

/* Hack: A hidden function in audio.c just for me. */
int audio_enc_name2code(const char* name);

static int intflag = FALSE;
#if !defined(WIN32) && !defined(GENERIC)
static void catch_interrupt(void)
{
        intflag = TRUE;
}
#endif

int main(int sys_argc, char ** sys_argv)
{
	int result;
	int encoding;
	size_t channels;
#if defined(WIN32)
	_setmode(STDIN_FILENO,  _O_BINARY);
#endif

#if defined (WANT_WIN32_UNICODE)
	if(win32_cmdline_utf8(&argc, &argv) != 0)
	{
		error("Cannot convert command line to UTF8!");
		safe_exit(76);
	}
#else
	argv = sys_argv;
	argc = sys_argc;
#endif

	if(!(fullprogname = strdup(argv[0])))
	{
		error("OOM"); /* Out Of Memory. Don't waste bytes on that error. */
		safe_exit(1);
	}
	/* Extract binary and path, take stuff before/after last / or \ . */
	if(  (prgName = strrchr(fullprogname, '/')) 
	  || (prgName = strrchr(fullprogname, '\\')))
	{
		/* There is some explicit path. */
		prgName[0] = 0; /* End byte for path. */
		prgName++;
		binpath = fullprogname;
	}
	else
	{
		prgName = fullprogname; /* No path separators there. */
		binpath = NULL; /* No path at all. */
	}

#ifdef OS2
        _wildcard(&argc,&argv);
#endif

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

	/* Ensure cleanup before we cause too much mess. */
#if !defined(WIN32) && !defined(GENERIC)
	catchsignal(SIGINT, catch_interrupt);
	catchsignal(SIGTERM, catch_interrupt);
#endif
	ao = out123_new();
	if(!ao){ error("Failed to allocate output."); exit(1); }

	if
	( 0
	||	out123_param(ao, OUT123_FLAGS, param.output_flags, 0.)
	|| out123_param(ao, OUT123_PRELOAD, 0, param.preload)
	|| out123_param(ao, OUT123_GAIN, param.gain, 0.)
	|| out123_param(ao, OUT123_VERBOSE, param.verbose, 0.)
	)
	{
		error("Error setting output parameters. Do you need a usage reminder?");
		usage(1);
	}
	

#ifdef HAVE_SETPRIORITY
	if(param.aggressive) { /* tst */
		int mypid = getpid();
		if(!param.quiet) fprintf(stderr,"Aggressively trying to increase priority.\n");
		if(setpriority(PRIO_PROCESS,mypid,-20))
			error("Failed to aggressively increase priority.\n");
	}
#endif

#if defined (HAVE_SCHED_SETSCHEDULER) && !defined (__CYGWIN__) && !defined (HAVE_WINDOWS_H)
/* Cygwin --realtime seems to fail when accessing network, using win32 set priority instead */
/* MinGW may have pthread installed, we prefer win32API */
	if (param.realtime)
	{  /* Get real-time priority */
		struct sched_param sp;
		if(!param.quiet) fprintf(stderr,"Getting real-time priority\n");
		memset(&sp, 0, sizeof(struct sched_param));
		sp.sched_priority = sched_get_priority_min(SCHED_FIFO);
		if (sched_setscheduler(0, SCHED_RR, &sp) == -1)
			error("Can't get realtime priority\n");
	}
#endif

/* make sure not Cygwin, it doesn't need it */
#if defined(WIN32) && defined(HAVE_WINDOWS_H)
	/* argument "3" is equivalent to realtime priority class */
	win32_set_priority( param.realtime ? 3 : param.w32_priority);
#endif

	encoding = audio_enc_name2code(param.force_encoding);
	channels = 2;
	if(frameflag & MPG123_FORCE_MONO) channels = 1;
	if(!encoding)
	{
		error1("Unknown encoding '%s' given!\n", param.force_encoding);
		safe_exit(1);
	}
	pcmframe = mpg123_encsize(encoding)*channels;
	bufferblock = pcmblock*pcmframe;
	audio = (unsigned char*) malloc(bufferblock);

	check_fatal_output(out123_set_buffer(ao, param.usebuffer*1024));
	/* This needs bufferblock set! */
	check_fatal_output(out123_open( ao
	,	param.output_module, param.output_device ));

	fprintf(stderr, "TODO: Check audio caps, add option to display 'em.\n");
	/* audio_capabilities(ao, mh); */

	check_fatal_output(out123_start(ao, encoding, channels, param.force_rate));

	input = stdin;
	while(play_frame() && !intflag)
	{
		/* be happy */
	}
	if(intflag) /* Make it quick! */
	{
		if(!param.quiet)
			fprintf(stderr, "Interrupted. Dropping the ball.\n");
		out123_drop(ao);
	}

	safe_exit(0); /* That closes output and restores terminal, too. */
	return 0;
}

static void print_title(FILE *o)
{
	fprintf(o, "Simple audio output with raw PCM input\n");
	fprintf(o, "\tversion %s; derived from mpg123 by Michael Hipp and others\n", PACKAGE_VERSION);
	fprintf(o, "\tfree software (LGPL) without any warranty but with best wishes\n");
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
	fprintf(o,"   -v    increase verbosity level       -q    quiet (only print errors)\n");
	fprintf(o,"   -t    testmode (no output)           -s    write to stdout\n");
	fprintf(o,"   -w f  write output as WAV file\n");
	fprintf(o,"   -b n  output buffer: n Kbytes [0]                                  \n");
	fprintf(o,"   -r n  set samplerate [44100]\n");
	fprintf(o,"   -o m  select output module           -a d  set audio device\n");
	fprintf(o,"   -m    single-channel (mono) instead of stereo\n");
	#ifdef HAVE_SCHED_SETSCHEDULER
	fprintf(o,"   -T get realtime priority\n");
	#endif
	fprintf(o,"   -?    this help                      --version  print name + version\n");
	fprintf(o,"See the manpage out123(1) or call %s with --longhelp for more parameters and information.\n", prgName);
	safe_exit(err);
}

static void want_usage(char* arg)
{
	usage(0);
}

static void long_usage(int err)
{
	char *enclist = NULL;
	FILE* o = stdout;
	audio_enclist(&enclist);
	if(err)
	{
  	o = stderr; 
  	fprintf(o, "You made some mistake in program usage... let me remind you:\n\n");
	}
	print_title(o);
	fprintf(o,"\nusage: %s [option(s)] [file(s) | URL(s) | -]\n", prgName);

	fprintf(o," -o <o> --output <o>       select audio output module\n");
	fprintf(o,"        --list-modules     list the available modules\n");
	fprintf(o," -a <d> --audiodevice <d>  select audio device\n");
	fprintf(o," -s     --stdout           write raw audio to stdout (host native format)\n");
	fprintf(o," -S     --STDOUT           play AND output stream (not implemented yet)\n");
	fprintf(o," -w <f> --wav <f>          write samples as WAV file in <f> (- is stdout)\n");
	fprintf(o,"        --au <f>           write samples as Sun AU file in <f> (- is stdout)\n");
	fprintf(o,"        --cdr <f>          write samples as raw CD audio file in <f> (- is stdout)\n");
	fprintf(o," -m     --mono             set channelcount to 1\n");
	fprintf(o,"        --stereo           set channelcount to 2 (default)\n");
	fprintf(o," -r <r> --rate <r>         set the audio output rate in Hz (default 44100)\n");
	fprintf(o," -e <c> --encoding <c>     set output encoding (%s)\n", enclist != NULL ? enclist : "OOM!");
	fprintf(o," -o h   --headphones       (aix/hp/sun) output on headphones\n");
	fprintf(o," -o s   --speaker          (aix/hp/sun) output on speaker\n");
	fprintf(o," -o l   --lineout          (aix/hp/sun) output to lineout\n");
#ifndef NOXFERMEM
	fprintf(o," -b <n> --buffer <n>       set play buffer (\"output cache\")\n");
	fprintf(o,"        --preload <value>  fraction of buffer to fill before playback\n");
#endif
	fprintf(o," -t     --test             no output, just read and discard data\n");
	fprintf(o," -v[*]  --verbose          increase verboselevel\n");
	#ifdef HAVE_SETPRIORITY
	fprintf(o,"        --aggressive       tries to get higher priority (nice)\n");
	#endif
	#if defined (HAVE_SCHED_SETSCHEDULER) || defined (HAVE_WINDOWS_H)
	fprintf(o," -T     --realtime         tries to get realtime priority\n");
	#endif
	#ifdef HAVE_WINDOWS_H
	fprintf(o,"        --priority <n>     use specified process priority\n");
	fprintf(o,"                           accepts -2 to 3 as integer arguments\n");
	fprintf(o,"                           -2 as idle, 0 as normal and 3 as realtime.\n");
	#endif
	fprintf(o," -?     --help             give compact help\n");
	fprintf(o,"        --longhelp         give this long help listing\n");
	fprintf(o,"        --version          give name / version string\n");

	fprintf(o,"\nSee the manpage out123(1) for more information.\n");
	safe_exit(err);
}

static void want_long_usage(char* arg)
{
	long_usage(0);
}

static void give_version(char* arg)
{
	fprintf(stdout, "out123 "PACKAGE_VERSION"\n");
	safe_exit(0);
}