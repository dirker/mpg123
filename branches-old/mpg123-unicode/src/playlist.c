/*
	playlist: playlist logic

	copyright 1995-2008 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Michael Hipp, outsourced/reorganized by Thomas Orgis

	If we officially support Windows again, we should have this reworked to really cope with Windows paths, too.
*/

#include "mpg123app.h"
#include "getlopt.h" /* for loptind */
#include "term.h" /* for term_restore */
#include "playlist.h"
#include "httpget.h"
#include <time.h> /* For srand(). */
#include "debug.h"

#ifdef HAVE_RANDOM
#define RAND random
#define SRAND srandom
#else
#define RAND rand
#define SRAND srand
#endif

/* increase linebuf in blocks of ... bytes */
#define LINEBUF_STEP 100

/* one global instance... add a pointer to this to every function definition and you have OO-style... */
playlist_struct pl;

/*
	functions to be called from outside and thus declared in the header:

	void prepare_playlist(int argc, char** argv);
	char *get_next_file(int argc, char **argv);
	void free_playlist();
*/

/* local functions */

int add_next_file (int argc, TCHAR *argv[]);
void shuffle_playlist();
void init_playlist();
int add_copy_to_playlist(TCHAR* new_entry);
int add_to_playlist(TCHAR* new_entry, char freeit);

/* used to be init_input */
void prepare_playlist(int argc, TCHAR** argv)
{
	/*
		fetch all playlist entries ... I don't consider playlists to be an endless stream.
		If you want to intentionally hang mpg123 on some other prog that may take infinite time to produce the full list (perhaps load tracks on demand), then just use the remote control and let that program print "load filename" instead of "filename".
		We may even provide a simple wrapper script that emulates the old playlist reading behaviour (for files and stdin, http playlists are actually a strong point on reading the list in _before_ starting playback since http connections don't last forever).
	*/
	init_playlist();
	while (add_next_file(argc, argv)) {}
	if(param.verbose > 1)
	{
		fprintf(stderr, "\nplaylist in normal order:\n");
		print_playlist(stderr, 0);
		fprintf(stderr, "\n");
	}
	if(param.shuffle == 1) shuffle_playlist();
	/* Don't need these anymore, we have copies! */
	mpg123_free_string(&pl.linebuf);
	mpg123_free_string(&pl.dir);
}

/* Return a random number >= 0 and < n */
static size_t rando(size_t n)
{
	long ran;
	long limit = RAND_MAX - (RAND_MAX % (long)n);
	if(n<2) return 0; /* Better settle that here than in an endless loop... */
	do{ ran = RAND(); }while( ran >= limit );
	return (size_t)(ran%n);
}

TCHAR *get_next_file()
{
	struct listitem *newitem = NULL;

	if(pl.fill == 0) return NULL;

	++pl.playcount;

	/* normal order, just pick next thing */
	if(param.shuffle < 2)
	{
		do
		{
			if(pl.pos < pl.fill) newitem = &pl.list[pl.pos];
			else newitem = NULL;
			/* if we have rounds left, decrease loop, else reinit loop because it's a new track */
			if(pl.loop > 0) --pl.loop; /* loop for current track... */
			if(pl.loop == 0)
			{
				pl.loop = param.loop;
				++pl.pos;
			}
		} while(pl.loop == 0 && newitem != NULL);
	}
	else
	{	/* Randomly select files, with repeating... but keep track of current track for playlist printing. */
		do /* limiting randomness: don't repeat too early */
		{
			pl.pos = rando(pl.fill);
		} while( pl.list[pl.pos].playcount && (pl.playcount - pl.list[pl.pos].playcount) <= pl.fill/2 );

		newitem = &pl.list[pl.pos];
	}

	/* "-" is STDOUT, "" is dumb, NULL is nothing */
	if(newitem != NULL)
	{
		/* Remember the playback position of the track. */
		newitem->playcount = pl.playcount;
		return newitem->url;
	}
	else return NULL;
}

/* It doesn't really matter on program exit, but anyway...
   Make sure you don't free() an item of argv! */
void free_playlist()
{
	if(pl.list != NULL)
	{
		debug("going to free() the playlist");
		while(pl.fill)
		{
			--pl.fill;
			debug1("free()ing entry %lu", (unsigned long)pl.fill);
			if(pl.list[pl.fill].freeit) free(pl.list[pl.fill].url);
		}
		free(pl.list);
		pl.list = NULL;
		pl.size = 0;
		debug("free()d the playlist");
	}
	mpg123_free_string(&pl.linebuf);
	mpg123_free_string(&pl.dir);
}

/* the constructor... */
void init_playlist()
{
	SRAND(time(NULL));
	pl.file = NULL;
	pl.entry = 0;
	pl.playcount = 0;
	pl.size = 0;
	pl.fill = 0;
	pl.pos = 0;
	pl.list = NULL;
	pl.alloc_step = 10;
	mpg123_init_string(&pl.dir);
	mpg123_init_string(&pl.linebuf);
	pl.type = UNKNOWN;
	pl.loop = param.loop;
}

/*
	slightly modified find_next_file from mpg123.c
	now doesn't return the next entry but adds it to playlist struct
	returns 1 if it found something, 0 on end
*/
int add_next_file (int argc, TCHAR *argv[])
{
	int firstline = 0;

	/* hack for url that has been detected as track, not playlist */
	if(pl.type == NO_LIST) return 0;

	/* Get playlist dirname to append it to the files in playlist */
	if (param.listname)
	{
		TCHAR* slashpos;
		/* Oh, right... that doesn't look good for Windows... */
		if ((slashpos=_tcsrchr(param.listname, __T('/'))))
		{
			/* up to and including /, with space for \0 */
			if(mpg123_resize_string(&pl.dir, 2 + slashpos - param.listname))
			{
				memcpy(pl.dir.p, param.listname, pl.dir.size-1);
				pl.dir.p[pl.dir.size-1] = 0;
			}
			else
			{
				error("cannot allocate memory for list directory!");
				pl.dir.size = 0;
			}
		}
	}

	if (param.listname || pl.file)
	{
		size_t line_offset = 0;
		if (!pl.file)
		{
			/* empty or "-" */
			if (!*param.listname || !_tcscmp(param.listname, __T("-")))
			{
				pl.file = stdin;
				param.listname = NULL;
				pl.entry = 0;
			}
			else if (!_tcsncmp(param.listname, __T("http://"), 7))
			{
				int fd;
				struct httpdata htd;
				httpdata_init(&htd);
				fd = http_open(param.listname, &htd);
				debug1("htd.content_type.p: %p", (void*) htd.content_type.p);
				if(!param.ignore_mime && htd.content_type.p != NULL)
				{
					int mimi;
					debug1("htd.content_type.p value: %s", htd.content_type.p);
					mimi = debunk_mime(htd.content_type.p);

					if(mimi & IS_M3U) pl.type = M3U;
					else if(mimi & IS_PLS)	pl.type = PLS;
					else
					{
						if(fd >= 0) close(fd);

						fd = -1;
						if(mimi & IS_FILE)
						{
							pl.type = NO_LIST;
							if(param.listentry < 0)
							{
								_tprintf(__T("#note you gave me a file url, no playlist, so...\n#entry 1\n%"strz"\n"), param.listname);
								return 0;
							}
							else
							{
								fprintf(stderr, "Note: MIME type indicates that this is no playlist but an mpeg audio file... reopening as such.\n");
								add_to_playlist(param.listname, 0);
								return 1;
							}
						}
						error1("Unknown playlist MIME type %s; maybe "PACKAGE_NAME" can support it in future if you report this to the maintainer.", htd.content_type.p);
					}
					httpdata_free(&htd);
				}
				if(fd < 0)
				{
					param.listname = NULL;
					pl.file = NULL;
					error("Invalid playlist from http_open()!\n");
				}
				else
				{
					pl.entry = 0;
					pl.file = fdopen(fd,"r");
				}
			}
			else if (!(pl.file = _tfopen(param.listname, __T("rb"))))
			{
				_tperror (param.listname);
				return 0;
			}
			else
			{
				debug("opened ordinary list file");
				pl.entry = 0;
			}
			if (param.verbose && pl.file) _ftprintf (stderr, __T("Using playlist from %"strz" ...\n"),	param.listname ? param.listname : __T("standard input"));
			firstline = 1; /* just opened */
		}
		/* reading the file line by line */
		while (pl.file)
		{
			/*
				now read a string of arbitrary size...
				read a chunk, see if lineend, realloc, read another chunk
				
				fgets reads at most size-1 bytes and always appends the \0 
			*/
			size_t have = 0;
			do
			{
				/* have is the length of the string read, without the closing \0 */
				if(pl.linebuf.size <= have+1)
				{
					if(!mpg123_resize_string(&pl.linebuf, pl.linebuf.size+LINEBUF_STEP))
					{
						error("cannot increase line buffer");
						break;
					}
				}
				/* I rely on fgets writing the \0 at the end! */
				if(_fgetts(pl.linebuf.p+have, pl.linebuf.size-have, pl.file))
				{
					have += _tcslen(pl.linebuf.p+have);
					debug2("have read %lu characters into linebuf: [%s]", (unsigned long)have, pl.linebuf.p);
				}
				else
				{
					debug("fgets failed to deliver something... file ended?");
					break;
				}
			} while(have && pl.linebuf.p[have-1] != '\r' && pl.linebuf.p[have-1] != '\n');
			if(have)
			{
				pl.linebuf.p[_tcscspn(pl.linebuf.p, __T("\t\n\r"))] = '\0';
				/* a bit of fuzzyness */
				if(firstline)
				{
					if(pl.type == UNKNOWN)
					{
						if(!_tcscmp(__T("[playlist]"), pl.linebuf.p))
						{
							fprintf(stderr, "Note: detected Shoutcast/Winamp PLS playlist\n");
							pl.type = PLS;
							continue;
						}
						else if
						(
							(!_tcsncicmp(__T("#M3U"), pl.linebuf.p ,4))
							||
							(!_tcsncicmp(__T("#EXTM3U"), pl.linebuf.p ,7))
							||
							(param.listname != NULL && (_tcsrchr(param.listname, __T('.'))) != NULL && !_tcsicmp(__T(".m3u"), _tcsrchr(param.listname, __T('.'))))
						)
						{
							if(param.verbose) fprintf(stderr, "Note: detected M3U playlist type\n");
							pl.type = M3U;
						}
						else
						{
							if(param.verbose) fprintf(stderr, "Note: guessed M3U playlist type\n");
							pl.type = M3U;
						}
					}
					else
					{
						if(param.verbose)
						{
							fprintf(stderr, "Note: Interpreting as ");
							switch(pl.type)
							{
								case M3U: fprintf(stderr, "M3U"); break;
								case PLS: fprintf(stderr, "PLS (Winamp/Shoutcast)"); break;
								default: fprintf(stderr, "???");
							}
							fprintf(stderr, " playlist\n");
						}
					}
					firstline = 0;
				}
				#if !defined(WIN32)
				{
				size_t i;
				/* convert \ to / (from MS-like directory format) */
				for (i=0;pl.linebuf.p[i]!='\0';i++)
				{
					if (pl.linebuf.p[i] == '\\')	pl.linebuf.p[i] = '/';
				}
				}
				#endif
				if (pl.linebuf.p[0]=='\0') continue; /* skip empty lines... */
				if (((pl.type == M3U) && (pl.linebuf.p[0]=='#')))
				{
					/* a comment line in m3u file */
					if(param.listentry < 0) _tprintf(__T("%"strz"\n"), pl.linebuf.p);
					continue;
				}

				/* real filename may start at an offset */
				line_offset = 0;
				/* extract path out of PLS */
				if(pl.type == PLS)
				{
					if(!_tcsncicmp(__T("File"), pl.linebuf.p, 4))
					{
						/* too lazy to really check for file number... would have to change logic to support unordered file entries anyway */
						TCHAR* in_line;
						if((in_line = _tcschr((pl.linebuf.p+4), __T('='))) != NULL)
						{
							/* FileN=? */
							if(in_line[1] != 0)
							{
								++in_line;
								line_offset = (size_t) (in_line-pl.linebuf.p);
							}
							else
							{
								fprintf(stderr, "Warning: Invalid PLS line (empty filename) - corrupt playlist file?\n");
								continue;
							}
						}
						else
						{
							fprintf(stderr, "Warning: Invalid PLS line (no '=' after 'File') - corrupt playlist file?\n");
							continue;
						}
					}
					else
					{
						if(param.listentry < 0) _tprintf(__T("#metainfo %"strz"\n"), pl.linebuf.p);
						continue;
					}
				}

				/* make paths absolute */
				/* Windows knows absolute paths with c: in front... should handle this if really supporting win32 again */
				if
				(
					(pl.dir.p != NULL)
					&& (pl.linebuf.p[line_offset]!=__T('/'))
					&& (pl.linebuf.p[line_offset]!=__T('\\'))
					&& _tcsncmp(pl.linebuf.p+line_offset, __T("http://"), 7)
				)
				{
					size_t need;
					need = pl.dir.size + _tcslen(pl.linebuf.p+line_offset);
					if(pl.linebuf.size < need)
					{
						if(!mpg123_resize_string(&pl.linebuf, need))
						{
							error("unable to enlarge linebuf for appending path! skipping");
							continue;
						}
					}
					/* move to have the space at beginning */
					memmove(pl.linebuf.p+pl.dir.size-1, pl.linebuf.p+line_offset, sizeof(TCHAR) * _tcslen((pl.linebuf.p+line_offset)+1));
					/* prepend path */
					memcpy(pl.linebuf.p, pl.dir.p, (pl.dir.size-1)*sizeof(TCHAR));
					line_offset = 0;
				}
				++pl.entry;
				if(param.listentry < 0) _tprintf(__T("#entry %lu\n%"strz"\n"), (unsigned long)pl.entry,pl.linebuf.p+line_offset);
				else if((param.listentry == 0) || (param.listentry == pl.entry))
				{
					add_copy_to_playlist(pl.linebuf.p+line_offset);
					return 1;
				}
			}
			else
			{
				if (param.listname)
				fclose (pl.file);
				param.listname = NULL;
				pl.file = NULL;
			}
		}
	}
	if(loptind < argc)
	{
		add_to_playlist(argv[loptind++], 0);
		return 1;
	}
	return 0;
}

void shuffle_playlist()
{
	size_t loop;
	size_t rannum;
	if(pl.fill >= 2)
	{
		/* Refer to bug 1777621 for discussion on that.
		   It's Durstenfeld... */
		for (loop = 0; loop < pl.fill; loop++)
		{
			struct listitem tmp;
			rannum = loop + rando(pl.fill-loop);
			/*
				Small test on your binary operation skills (^ is XOR):
				a = b^(a^b)
				b = (a^b)^(b^(a^b))
				And, understood? ;-)
				
				pl.list[loop] ^= pl.list[rannum];
				pl.list[rannum] ^= pl.list[loop];
				pl.list[loop] ^= pl.list[rannum];
				
				But since this is not allowed with pointers and any speed gain questionable (well, saving _some_ memory...), doing it the lame way:
			*/
			tmp = pl.list[rannum];
			pl.list[rannum] = pl.list[loop];
			pl.list[loop] = tmp;
		}
	}

	if(param.verbose > 1)
	{
		/* print them */
		fprintf(stderr, "\nshuffled playlist:\n");
		print_playlist(stderr, 0);
		fprintf(stderr, "\n");
	}
}

void print_playlist(FILE* out, int showpos)
{
	size_t loop;
	for (loop = 0; loop < pl.fill; loop++)
	{
		char *pre = "";
		if(showpos)
		pre = (pl.pos>0 && loop==pl.pos-1) ? "> " : "  ";

		_ftprintf(out, __T("%s%"strz"\n"), pre, pl.list[loop].url);
	}
}


int add_copy_to_playlist(TCHAR* new_entry)
{
	TCHAR* cop;
	if ((cop = (TCHAR*) malloc(sizeof (TCHAR) * (_tcslen(new_entry)+1))) != NULL)
	{
		_tcscpy(cop, new_entry);
		return add_to_playlist(cop, 1);
	}
	else return 0;
}

/* add new entry to playlist - no string copy, just the pointer! */
int add_to_playlist(TCHAR* new_entry, char freeit)
{
	if(pl.fill == pl.size)
	{
		struct listitem* tmp = NULL;
		/* enlarge the list */
		tmp = (struct listitem*) safe_realloc(pl.list, (pl.size + pl.alloc_step) * sizeof(struct listitem));
		if(!tmp)
		{
			error("unable to allocate more memory for playlist");
			perror("");
			return 0;
		}
		else
		{
			pl.list = tmp;
			pl.size += pl.alloc_step;
		}
	}
	/* paranoid */
	if(pl.fill < pl.size)
	{
		pl.list[pl.fill].freeit = freeit;
		pl.list[pl.fill].url = new_entry;
		pl.list[pl.fill].playcount = 0;
		++pl.fill;
	}
	else
	{
		error("playlist memory still too small?!");
		return 0;
	}
	return 1;
}
