/*
	frame: Heap of routines dealing with the core mpg123 data structure.

	copyright 2008-9 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org
	initially written by Thomas Orgis
*/

#include "mpg123lib_intern.h"
#include "getcpuflags.h"
#include "debug.h"

static void frame_fixed_reset(mpg123_handle *fr);

/* that's doubled in decode_ntom.c */
#define NTOM_MUL (32768)
#define aligned_pointer(p,type,alignment) \
	(((char*)(p)-(char*)NULL) % (alignment)) \
	? (type*)((char*)(p) + (alignment) - (((char*)(p)-(char*)NULL) % (alignment))) \
	: (type*)(p)
void frame_default_pars(mpg123_pars *mp)
{
	mp->outscale = 1.0;
#ifdef GAPLESS
	mp->flags = MPG123_GAPLESS;
#else
	mp->flags = 0;
#endif
#ifndef NO_NTOM
	mp->force_rate = 0;
#endif
	mp->down_sample = 0;
	mp->rva = 0;
	mp->halfspeed = 0;
	mp->doublespeed = 0;
	mp->verbose = 0;
#ifndef NO_ICY
	mp->icy_interval = 0;
#endif
#ifndef WIN32
	mp->timeout = 0;
#endif
	mp->resync_limit = 1024;
#ifdef FRAME_INDEX
	mp->index_size = INDEX_SIZE;
#endif
	mp->preframes = 4; /* That's good  for layer 3 ISO compliance bitstream. */
	mpg123_fmt_all(mp);
}

void frame_init(mpg123_handle *fr)
{
	frame_init_par(fr, NULL);
}

void frame_init_par(mpg123_handle *fr, mpg123_pars *mp)
{
	fr->own_buffer = FALSE;
	fr->buffer.data = NULL;
	fr->rawbuffs = NULL;
	fr->rawbuffss = 0;
	fr->rawdecwin = NULL;
	fr->rawdecwins = 0;
#ifndef NO_8BIT
	fr->conv16to8_buf = NULL;
#endif
#ifdef OPT_DITHER
	fr->dithernoise = NULL;
#endif
	fr->xing_toc = NULL;
	fr->cpu_opts.type = defdec();
	fr->cpu_opts.class = decclass(fr->cpu_opts.type);
#ifndef NO_NTOM
	/* these two look unnecessary, check guarantee for synth_ntom_set_step (in control_generic, even)! */
	fr->ntom_val[0] = NTOM_MUL>>1;
	fr->ntom_val[1] = NTOM_MUL>>1;
	fr->ntom_step = NTOM_MUL;
#endif
	/* unnecessary: fr->buffer.size = fr->buffer.fill = 0; */
	mpg123_reset_eq(fr);
	init_icy(&fr->icy);
	init_id3(fr);
	/* frame_outbuffer is missing... */
	/* frame_buffers is missing... that one needs cpu opt setting! */
	/* after these... frame_reset is needed before starting full decode */
	invalidate_format(&fr->ps.of);
	fr->rdat.r_read = NULL;
	fr->rdat.r_lseek = NULL;
	fr->decoder_change = 1;
	fr->ps.err = MPG123_OK;
	if(mp == NULL) frame_default_pars(&fr->p);
	else memcpy(&fr->p, mp, sizeof(struct mpg123_pars_struct));

	fr->down_sample = 0; /* Initialize to silence harmless errors when debugging. */
	frame_fixed_reset(fr); /* Reset only the fixed data, dynamic buffers are not there yet! */
	fr->synth = NULL;
	fr->synth_mono = NULL;
	fr->make_decode_tables = NULL;
#ifdef FRAME_INDEX
	fi_init(&fr->index);
	frame_index_setup(fr); /* Apply the size setting. */
#endif
}

#ifdef OPT_DITHER
/* Also, only allocate the memory for the table on demand.
   In future, one could create special noise for different sampling frequencies(?). */
int frame_dither_init(mpg123_handle *fr)
{
	/* run-time dither noise table generation */
	if(fr->dithernoise == NULL)
	{
		fr->dithernoise = malloc(sizeof(float)*DITHERSIZE);
		if(fr->dithernoise == NULL) return 0;

		dither_table_init(fr->dithernoise);
	}
	return 1;
}
#endif

mpg123_pars attribute_align_arg *mpg123_new_pars(int *error)
{
	mpg123_pars *mp = malloc(sizeof(struct mpg123_pars_struct));
	if(mp != NULL){ frame_default_pars(mp); if(error != NULL) *error = MPG123_OK; }
	else if(error != NULL) *error = MPG123_OUT_OF_MEM;
	return mp;
}

void attribute_align_arg mpg123_delete_pars(mpg123_pars* mp)
{
	if(mp != NULL) free(mp);
}

int attribute_align_arg mpg123_reset_eq(mpg123_handle *mh)
{
	int i;
	mh->have_eq_settings = 0;
	for(i=0; i < 32; ++i) mh->equalizer[0][i] = mh->equalizer[1][i] = DOUBLE_TO_REAL(1.0);

	return MPG123_OK;
}

int frame_outbuffer(mpg123_handle *fr)
{
	size_t size = mpg123_safe_buffer()*AUDIOBUFSIZE;
	if(!fr->own_buffer) fr->buffer.data = NULL;
	if(fr->buffer.data != NULL && fr->buffer.size != size)
	{
		free(fr->buffer.data);
		fr->buffer.data = NULL;
	}
	fr->buffer.size = size;
	if(fr->buffer.data == NULL) fr->buffer.data = (unsigned char*) malloc(fr->buffer.size);
	if(fr->buffer.data == NULL)
	{
		fr->ps.err = MPG123_OUT_OF_MEM;
		return -1;
	}
	fr->own_buffer = TRUE;
	fr->buffer.fill = 0;
	return 0;
}

int attribute_align_arg mpg123_replace_buffer(mpg123_handle *mh, unsigned char *data, size_t size)
{
	if(data == NULL || size < mpg123_safe_buffer())
	{
		mh->ps.err = MPG123_BAD_BUFFER;
		return MPG123_ERR;
	}
	if(mh->own_buffer && mh->buffer.data != NULL) free(mh->buffer.data);
	mh->own_buffer = FALSE;
	mh->buffer.data = data;
	mh->buffer.size = size;
	mh->buffer.fill = 0;
	return MPG123_OK;
}

#ifdef FRAME_INDEX
int frame_index_setup(mpg123_handle *fr)
{
	int ret = MPG123_ERR;
	if(fr->p.index_size >= 0)
	{ /* Simple fixed index. */
		fr->index.grow_size = 0;
		debug1("resizing index to %li", fr->p.index_size);
		ret = fi_resize(&fr->index, (size_t)fr->p.index_size);
		debug2("index resized... %lu at %p", (unsigned long)fr->index.size, (void*)fr->index.data);
	}
	else
	{ /* A growing index. We give it a start, though. */
		fr->index.grow_size = (size_t)(- fr->p.index_size);
		if(fr->index.size < fr->index.grow_size)
		ret = fi_resize(&fr->index, fr->index.grow_size);
		else
		ret = MPG123_OK; /* We have minimal size already... and since growing is OK... */
	}
	debug2("set up frame index of size %lu (ret=%i)", (unsigned long)fr->index.size, ret);

	return ret;
}
#endif

static void frame_decode_buffers_reset(mpg123_handle *fr)
{
	memset(fr->rawbuffs, 0, fr->rawbuffss);
}

int frame_buffers(mpg123_handle *fr)
{
	int buffssize = 0;
	debug1("frame %p buffer", (void*)fr);
/*
	the used-to-be-static buffer of the synth functions, has some subtly different types/sizes

	2to1, 4to1, ntom, generic, i386: real[2][2][0x110]
	mmx, sse: short[2][2][0x110]
	i586(_dither): 4352 bytes; int/long[2][2][0x110]
	i486: int[2][2][17*FIR_BUFFER_SIZE]
	altivec: static real __attribute__ ((aligned (16))) buffs[4][4][0x110]

	Huh, altivec looks like fun. Well, let it be large... then, the 16 byte alignment seems to be implicit on MacOSX malloc anyway.
	Let's make a reasonable attempt to allocate enough memory...
	Keep in mind: biggest ones are i486 and altivec (mutually exclusive!), then follows i586 and normal real.
	mmx/sse use short but also real for resampling.
	Thus, minimum is 2*2*0x110*sizeof(real).
*/
	if(fr->cpu_opts.type == altivec) buffssize = 4*4*0x110*sizeof(real);
#ifdef OPT_I486
	else if(fr->cpu_opts.type == ivier) buffssize = 2*2*17*FIR_BUFFER_SIZE*sizeof(int);
#endif
	else if(fr->cpu_opts.type == ifuenf || fr->cpu_opts.type == ifuenf_dither || fr->cpu_opts.type == dreidnow)
	buffssize = 2*2*0x110*4; /* don't rely on type real, we need 4352 bytes */

	if(2*2*0x110*sizeof(real) > buffssize)
	buffssize = 2*2*0x110*sizeof(real);
	buffssize += 15; /* For 16-byte alignment (SSE likes that). */

	if(fr->rawbuffs != NULL && fr->rawbuffss != buffssize)
	{
		free(fr->rawbuffs);
		fr->rawbuffs = NULL;
	}

	if(fr->rawbuffs == NULL) fr->rawbuffs = (unsigned char*) malloc(buffssize);
	if(fr->rawbuffs == NULL) return -1;
	fr->rawbuffss = buffssize;
	fr->short_buffs[0][0] = aligned_pointer(fr->rawbuffs,short,16);
	fr->short_buffs[0][1] = fr->short_buffs[0][0] + 0x110;
	fr->short_buffs[1][0] = fr->short_buffs[0][1] + 0x110;
	fr->short_buffs[1][1] = fr->short_buffs[1][0] + 0x110;
	fr->real_buffs[0][0] = aligned_pointer(fr->rawbuffs,real,16);
	fr->real_buffs[0][1] = fr->real_buffs[0][0] + 0x110;
	fr->real_buffs[1][0] = fr->real_buffs[0][1] + 0x110;
	fr->real_buffs[1][1] = fr->real_buffs[1][0] + 0x110;
#ifdef OPT_I486
	if(fr->cpu_opts.type == ivier)
	{
		fr->int_buffs[0][0] = (int*) fr->rawbuffs;
		fr->int_buffs[0][1] = fr->int_buffs[0][0] + 17*FIR_BUFFER_SIZE;
		fr->int_buffs[1][0] = fr->int_buffs[0][1] + 17*FIR_BUFFER_SIZE;
		fr->int_buffs[1][1] = fr->int_buffs[1][0] + 17*FIR_BUFFER_SIZE;
	}
#endif
#ifdef OPT_ALTIVEC
	if(fr->cpu_opts.type == altivec)
	{
		int i,j;
		fr->areal_buffs[0][0] = (real*) fr->rawbuffs;
		for(i=0; i<4; ++i) for(j=0; j<4; ++j)
		fr->areal_buffs[i][j] = fr->areal_buffs[0][0] + (i*4+j)*0x110;
	}
#endif
	/* now the different decwins... all of the same size, actually */
	/* The MMX ones want 32byte alignment, which I'll try to ensure manually */
	{
		int decwin_size = (512+32)*sizeof(real);
#ifdef OPT_MMXORSSE
#ifdef OPT_MULTI
		if(fr->cpu_opts.class == mmxsse)
		{
#endif
			/* decwin_mmx will share, decwins will be appended ... sizeof(float)==4 */
			if(decwin_size < (512+32)*4) decwin_size = (512+32)*4;

			/* the second window + alignment zone -- we align for 32 bytes for SSE as
			   requirement, 64 byte for matching cache line size (that matters!) */
			decwin_size += (512+32)*4 + 63;
			/* (512+32)*4/32 == 2176/32 == 68, so one decwin block retains alignment for 32 or 64 bytes */
#ifdef OPT_MULTI
		}
#endif
#endif
#if defined(OPT_ALTIVEC) || defined(OPT_ARM) 
		if(decwin_size < (512+32)*4) decwin_size = (512+32)*4;
		decwin_size += 512*4;
#endif
		/* Hm, that's basically realloc() ... */
		if(fr->rawdecwin != NULL && fr->rawdecwins != decwin_size)
		{
			free(fr->rawdecwin);
			fr->rawdecwin = NULL;
		}

		if(fr->rawdecwin == NULL)
		fr->rawdecwin = (unsigned char*) malloc(decwin_size);

		if(fr->rawdecwin == NULL) return -1;

		fr->rawdecwins = decwin_size;
		fr->decwin = (real*) fr->rawdecwin;
#ifdef OPT_MMXORSSE
#ifdef OPT_MULTI
		if(fr->cpu_opts.class == mmxsse)
		{
#endif
			/* align decwin, assign that to decwin_mmx, append decwins */
			/* I need to add to decwin what is missing to the next full 64 byte -- also I want to make gcc -pedantic happy... */
			fr->decwin = aligned_pointer(fr->rawdecwin,real,64);
			debug1("aligned decwin: %p", (void*)fr->decwin);
			fr->decwin_mmx = (float*)fr->decwin;
			fr->decwins = fr->decwin_mmx+512+32;
#ifdef OPT_MULTI
		}
		else debug("no decwins/decwin_mmx for that class");
#endif
#endif
	}
	/* Only reset the buffers we created just now. */
	frame_decode_buffers_reset(fr);

	debug1("frame %p buffer done", (void*)fr);
	return 0;
}

int frame_buffers_reset(mpg123_handle *fr)
{
	fr->buffer.fill = 0; /* hm, reset buffer fill... did we do a flush? */
	fr->bsnum = 0;
	/* Wondering: could it be actually _wanted_ to retain buffer contents over different files? (special gapless / cut stuff) */
	fr->bsbuf = fr->bsspace[1];
	fr->bsbufold = fr->bsbuf;
	fr->bitreservoir = 0; /* Not entirely sure if this is the right place for that counter. */
	frame_decode_buffers_reset(fr);
	memset(fr->bsspace, 0, 2*(MAXFRAMESIZE+512));
	memset(fr->ssave, 0, 34);
	fr->hybrid_blc[0] = fr->hybrid_blc[1] = 0;
	memset(fr->hybrid_block, 0, sizeof(real)*2*2*SBLIMIT*SSLIMIT);
	return 0;
}

void frame_icy_reset(mpg123_handle* fr)
{
#ifndef NO_ICY
	if(fr->icy.data != NULL) free(fr->icy.data);
	fr->icy.data = NULL;
	fr->icy.interval = 0;
	fr->icy.next = 0;
#endif
}

void frame_free_toc(mpg123_handle *fr)
{
	if(fr->xing_toc != NULL){ free(fr->xing_toc); fr->xing_toc = NULL; }
}

/* Just copy the Xing TOC over... */
int frame_fill_toc(mpg123_handle *fr, unsigned char* in)
{
	if(fr->xing_toc == NULL) fr->xing_toc = malloc(100);
	if(fr->xing_toc != NULL)
	{
		memcpy(fr->xing_toc, in, 100);
#ifdef DEBUG
		debug("Got a TOC! Showing the values...");
		{
			int i;
			for(i=0; i<100; ++i)
			debug2("entry %i = %i", i, fr->xing_toc[i]);
		}
#endif
		return TRUE;
	}
	return FALSE;
}

/* Prepare the handle for a new track.
   Reset variables, buffers... */
int frame_reset(mpg123_handle* fr)
{
	frame_buffers_reset(fr);
	frame_fixed_reset(fr);
	frame_free_toc(fr);
#ifdef FRAME_INDEX
	fi_reset(&fr->index);
#endif

	return 0;
}

/* Reset everythign except dynamic memory. */
static void frame_fixed_reset(mpg123_handle *fr)
{
	frame_icy_reset(fr);
	open_bad(fr);
	fr->ps.size = sizeof(struct mpg123_raw_state);
	fr->to_decode = FALSE;
	fr->to_ignore = FALSE;
	fr->metaflags = 0;
	fr->outblock = mpg123_safe_buffer();
	fr->ps.num = -1;
	fr->playnum = -1;
	fr->accurate = TRUE;
	fr->silent_resync = 0;
	fr->ps.audio_start = 0;
	fr->clip = 0;
	fr->oldhead = 0;
	fr->firsthead = 0;
	fr->ps.vbr = MPG123_CBR;
	fr->abr_rate = 0;
	fr->ps.track_frames = 0;
	fr->track_samples = -1;
	fr->ps.framesize=0; 
	fr->mean_frames = 0;
	fr->mean_framesize = 0;
	fr->lastscale = -1;
	fr->rva.level[0] = -1;
	fr->rva.level[1] = -1;
	fr->rva.gain[0] = 0;
	fr->rva.gain[1] = 0;
	fr->rva.peak[0] = 0;
	fr->rva.peak[1] = 0;
	fr->fsizeold = 0;
	fr->firstframe = 0;
	fr->ignoreframe = fr->firstframe-fr->p.preframes;
	fr->lastframe = -1;
	fr->fresh = 1;
	fr->ps.new_format = 0;
#ifdef GAPLESS
	frame_gapless_init(fr,0,0);
	fr->lastoff = 0;
	fr->firstoff = 0;
#endif
#ifdef OPT_I486
	fr->i486bo[0] = fr->i486bo[1] = FIR_SIZE-1;
#endif
	fr->bo = 1; /* the usual bo */
#ifdef OPT_DITHER
	fr->ditherindex = 0;
#endif
	reset_id3(fr);
	reset_icy(&fr->icy);
	/* ICY stuff should go into icy.c, eh? */
#ifndef NO_ICY
	fr->icy.interval = 0;
	fr->icy.next = 0;
#endif
	fr->halfphase = 0; /* here or indeed only on first-time init? */
	fr->ps.error_protection = 0;
	fr->freeformat_framesize = -1;
}

void frame_free_buffers(mpg123_handle *fr)
{
	if(fr->rawbuffs != NULL) free(fr->rawbuffs);
	fr->rawbuffs = NULL;
	fr->rawbuffss = 0;
	if(fr->rawdecwin != NULL) free(fr->rawdecwin);
	fr->rawdecwin = NULL;
	fr->rawdecwins = 0;
#ifndef NO_8BIT
	if(fr->conv16to8_buf != NULL) free(fr->conv16to8_buf);
	fr->conv16to8_buf = NULL;
#endif
}

void frame_exit(mpg123_handle *fr)
{
	if(fr->own_buffer && fr->buffer.data != NULL)
	{
		debug1("freeing buffer at %p", (void*)fr->buffer.data);
		free(fr->buffer.data);
	}
	fr->buffer.data = NULL;
	frame_free_buffers(fr);
	frame_free_toc(fr);
#ifdef FRAME_INDEX
	fi_exit(&fr->index);
#endif
#ifdef OPT_DITHER
	if(fr->dithernoise != NULL)
	{
		free(fr->dithernoise);
		fr->dithernoise = NULL;
	}
#endif
	exit_id3(fr);
	clear_icy(&fr->icy);
}

int attribute_align_arg mpg123_get_raw_state(mpg123_handle *mh, struct mpg123_raw_state **ps)
{
	if(mh == NULL) return MPG123_ERR;
	if(mh->ps.size < (*ps)->size) return MPG123_ERR;
	*ps = &mh->ps;
	return MPG123_OK;
}

int attribute_align_arg mpg123_info(mpg123_handle *mh, struct mpg123_frameinfo *mi)
{
	if(mh == NULL) return MPG123_ERR;
	if(mi == NULL)
	{
		mh->ps.err = MPG123_ERR_NULL;
		return MPG123_ERR;
	}
	mi->version = mh->ps.mpeg25 ? MPG123_2_5 : (mh->ps.lsf ? MPG123_2_0 : MPG123_1_0);
	mi->layer = mh->ps.lay;
	mi->rate = frame_freq(mh);
	switch(mh->ps.mode)
	{
		case 0: mi->mode = MPG123_M_STEREO; break;
		case 1: mi->mode = MPG123_M_JOINT;  break;
		case 2: mi->mode = MPG123_M_DUAL;   break;
		case 3: mi->mode = MPG123_M_MONO;   break;
		default: error("That mode cannot be!");
	}
	mi->mode_ext = mh->ps.mode_ext;
	mi->framesize = mh->ps.framesize+4; /* Include header. */
	mi->flags = 0;
	if(mh->ps.error_protection) mi->flags |= MPG123_CRC;
	if(mh->ps.copyright)        mi->flags |= MPG123_COPYRIGHT;
	if(mh->ps.extension)        mi->flags |= MPG123_PRIVATE;
	if(mh->ps.original)         mi->flags |= MPG123_ORIGINAL;
	mi->emphasis = mh->ps.emphasis;
	mi->bitrate  = frame_bitrate(mh);
	mi->abr_rate = mh->abr_rate;
	mi->vbr = mh->ps.vbr;
	return MPG123_OK;
}


/*
	Fuzzy frame offset searching (guessing).
	When we don't have an accurate position, we may use an inaccurate one.
	Possibilities:
		- use approximate positions from Xing TOC (not yet parsed)
		- guess wildly from mean framesize and offset of first frame / beginning of file.
*/

off_t frame_fuzzy_find(mpg123_handle *fr, off_t want_frame, off_t* get_frame)
{
	/* Default is to go to the beginning. */
	off_t ret = fr->ps.audio_start;
	*get_frame = 0;

	/* But we try to find something better. */
	/* Xing VBR TOC works with relative positions, both in terms of audio frames and stream bytes.
	   Thus, it only works when whe know the length of things.
	   Oh... I assume the offsets are relative to the _total_ file length. */
	if(fr->xing_toc != NULL && fr->ps.track_frames > 0 && fr->rdat.filelen > 0)
	{
		/* One could round... */
		int toc_entry = (int) ((double)want_frame*100./fr->ps.track_frames);
		/* It is an index in the 100-entry table. */
		if(toc_entry < 0)  toc_entry = 0;
		if(toc_entry > 99) toc_entry = 99;

		/* Now estimate back what frame we get. */
		*get_frame = (off_t) ((double)toc_entry/100. * fr->ps.track_frames);
		fr->accurate = FALSE;
		fr->silent_resync = 1;
		/* Question: Is the TOC for whole file size (with/without ID3) or the "real" audio data only?
		   ID3v1 info could also matter. */
		ret = (off_t) ((double)fr->xing_toc[toc_entry]/256.* fr->rdat.filelen);
	}
	else if(fr->mean_framesize > 0)
	{	/* Just guess with mean framesize (may be exact with CBR files). */
		/* Query filelen here or not? */
		fr->accurate = FALSE; /* Fuzzy! */
		fr->silent_resync = 1;
		*get_frame = want_frame;
		ret = (off_t) (fr->ps.audio_start+fr->mean_framesize*want_frame);
	}
	debug5("fuzzy: want %li of %li, get %li at %li B of %li B",
		(long)want_frame, (long)fr->ps.track_frames, (long)*get_frame, (long)ret, (long)(fr->rdat.filelen-fr->ps.audio_start));
	return ret;
}

/*
	find the best frame in index just before the wanted one, seek to there
	then step to just before wanted one with read_frame
	do not care tabout the stuff that was in buffer but not played back
	everything that left the decoder is counted as played
	
	Decide if you want low latency reaction and accurate timing info or stable long-time playback with buffer!
*/

off_t frame_index_find(mpg123_handle *fr, off_t want_frame, off_t* get_frame)
{
	/* default is file start if no index position */
	off_t gopos = 0;
	*get_frame = 0;
#ifdef FRAME_INDEX
	/* Possibly use VBRI index, too? I'd need an example for this... */
	if(fr->index.fill)
	{
		/* find in index */
		size_t fi;
		/* at index fi there is frame step*fi... */
		fi = want_frame/fr->index.step;
		if(fi >= fr->index.fill) /* If we are beyond the end of frame index...*/
		{
			/* When fuzzy seek is allowed, we have some limited tolerance for the frames we want to read rather then jump over. */
			if(fr->p.flags & MPG123_FUZZY && want_frame - (fr->index.fill-1)*fr->index.step > 10)
			{
				gopos = frame_fuzzy_find(fr, want_frame, get_frame);
				if(gopos > fr->ps.audio_start) return gopos; /* Only in that case, we have a useful guess. */
				/* Else... just continue, fuzzyness didn't help. */
			}
			/* Use the last available position, slowly advancing from that one. */
			fi = fr->index.fill - 1;
		}
		/* We have index position, that yields frame and byte offsets. */
		*get_frame = fi*fr->index.step;
		gopos = fr->index.data[fi];
		fr->accurate = TRUE; /* When using the frame index, we are accurate. */
	}
	else
	{
#endif
		if(fr->p.flags & MPG123_FUZZY)
		return frame_fuzzy_find(fr, want_frame, get_frame);
		/* A bit hackish here... but we need to be fresh when looking for the first header again. */
		fr->firsthead = 0;
		fr->oldhead = 0;
#ifdef FRAME_INDEX
	}
#endif
	debug2("index: 0x%lx for frame %li", (unsigned long)gopos, (long) *get_frame);
	return gopos;
}

off_t frame_ins2outs(mpg123_handle *fr, off_t ins)
{	
	off_t outs = 0;
	switch(fr->down_sample)
	{
		case 0:
#		ifndef NO_DOWNSAMPLE
		case 1:
		case 2:
#		endif
			outs = ins>>fr->down_sample;
		break;
#		ifndef NO_NTOM
		case 3: outs = ntom_ins2outs(fr, ins); break;
#		endif
		default: error1("Bad down_sample (%i) ... should not be possible!!", fr->down_sample);
	}
	return outs;
}

off_t frame_outs(mpg123_handle *fr, off_t num)
{
	off_t outs = 0;
	switch(fr->down_sample)
	{
		case 0:
#		ifndef NO_DOWNSAMPLE
		case 1:
		case 2:
#		endif
			outs = (spf(fr)>>fr->down_sample)*num;
		break;
#ifndef NO_NTOM
		case 3: outs = ntom_frmouts(fr, num); break;
#endif
		default: error1("Bad down_sample (%i) ... should not be possible!!", fr->down_sample);
	}
	return outs;
}

/* Compute the number of output samples we expect from this frame.
   This is either simple spf() or a tad more elaborate for ntom. */
off_t frame_expect_outsamples(mpg123_handle *fr)
{
	off_t outs = 0;
	switch(fr->down_sample)
	{
		case 0:
#		ifndef NO_DOWNSAMPLE
		case 1:
		case 2:
#		endif
			outs = spf(fr)>>fr->down_sample;
		break;
#ifndef NO_NTOM
		case 3: outs = ntom_frame_outsamples(fr); break;
#endif
		default: error1("Bad down_sample (%i) ... should not be possible!!", fr->down_sample);
	}
	return outs;
}

off_t frame_offset(mpg123_handle *fr, off_t outs)
{
	off_t num = 0;
	switch(fr->down_sample)
	{
		case 0:
#		ifndef NO_DOWNSAMPLE
		case 1:
		case 2:
#		endif
			num = outs/(spf(fr)>>fr->down_sample);
		break;
#ifndef NO_NTOM
		case 3: num = ntom_frameoff(fr, outs); break;
#endif
		default: error("Bad down_sample ... should not be possible!!");
	}
	return num;
}

#ifdef GAPLESS
/* input in _input_ samples */
void frame_gapless_init(mpg123_handle *fr, off_t b, off_t e)
{
	fr->begin_s = b;
	fr->end_s = e;
	/* These will get proper values later, from above plus resampling info. */
	fr->begin_os = 0;
	fr->end_os = 0;
	debug2("frame_gapless_init: from %lu to %lu samples", (long unsigned)fr->begin_s, (long unsigned)fr->end_s);
}

void frame_gapless_realinit(mpg123_handle *fr)
{
	fr->begin_os = frame_ins2outs(fr, fr->begin_s);
	fr->end_os   = frame_ins2outs(fr, fr->end_s);
	debug2("frame_gapless_realinit: from %lu to %lu samples", (long unsigned)fr->begin_os, (long unsigned)fr->end_os);
}

/* When we got a new sample count, update the gaplessness. */
void frame_gapless_update(mpg123_handle *fr, off_t total_samples)
{
	if(fr->end_s < 1)
	{
		fr->end_s = total_samples;
		frame_gapless_realinit(fr);
	}
	else if(fr->end_s > total_samples)
	{
		if(NOQUIET) error2("end sample count smaller than gapless end! (%"OFF_P" < %"OFF_P").", (off_p)total_samples, (off_p)fr->end_s);
		fr->end_s = total_samples;
	}
}

#endif

/* Compute the needed frame to ignore from, for getting accurate/consistent output for intended firstframe. */
static off_t ignoreframe(mpg123_handle *fr)
{
	off_t preshift = fr->p.preframes;
	/* Layer 3 _really_ needs at least one frame before. */
	if(fr->ps.lay==3 && preshift < 1) preshift = 1;
	/* Layer 1 & 2 reall do not need more than 2. */
	if(fr->ps.lay!=3 && preshift > 2) preshift = 2;

	return fr->firstframe - preshift;
}

/* The frame seek... This is not simply the seek to fe*spf(fr) samples in output because we think of _input_ frames here.
   Seek to frame offset 1 may be just seek to 200 samples offset in output since the beginning of first frame is delay/padding.
   Hm, is that right? OK for the padding stuff, but actually, should the decoder delay be better totally hidden or not?
   With gapless, even the whole frame position could be advanced further than requested (since Homey don't play dat). */
void frame_set_frameseek(mpg123_handle *fr, off_t fe)
{
	fr->firstframe = fe;
#ifdef GAPLESS
	if(fr->p.flags & MPG123_GAPLESS)
	{
		/* Take care of the beginning... */
		off_t beg_f = frame_offset(fr, fr->begin_os);
		if(fe <= beg_f)
		{
			fr->firstframe = beg_f;
			fr->firstoff   = fr->begin_os - frame_outs(fr, beg_f);
		}
		else fr->firstoff = 0;
		/* The end is set once for a track at least, on the frame_set_frameseek called in get_next_frame() */
		if(fr->end_os > 0)
		{
			fr->lastframe  = frame_offset(fr,fr->end_os);
			fr->lastoff    = fr->end_os - frame_outs(fr, fr->lastframe);
		} else fr->lastoff = 0;
	} else { fr->firstoff = fr->lastoff = 0; fr->lastframe = -1; }
#endif
	fr->ignoreframe = ignoreframe(fr);
#ifdef GAPLESS
	debug5("frame_set_frameseek: begin at %li frames and %li samples, end at %li and %li; ignore from %li",
	       (long) fr->firstframe, (long) fr->firstoff,
	       (long) fr->lastframe,  (long) fr->lastoff, (long) fr->ignoreframe);
#else
	debug3("frame_set_frameseek: begin at %li frames, end at %li; ignore from %li",
	       (long) fr->firstframe, (long) fr->lastframe, (long) fr->ignoreframe);
#endif
}

void frame_skip(mpg123_handle *fr)
{
#ifndef NO_LAYER3
	if(fr->ps.lay == 3) set_pointer(fr, 512);
#endif
}

/* Sample accurate seek prepare for decoder. */
/* This gets unadjusted output samples and takes resampling into account */
void frame_set_seek(mpg123_handle *fr, off_t sp)
{
	fr->firstframe = frame_offset(fr, sp);
#ifndef NO_NTOM
	if(fr->down_sample == 3) ntom_set_ntom(fr, fr->firstframe);
#endif
	fr->ignoreframe = ignoreframe(fr);
#ifdef GAPLESS /* The sample offset is used for non-gapless mode, too! */
	fr->firstoff = sp - frame_outs(fr, fr->firstframe);
	debug5("frame_set_seek: begin at %li frames and %li samples, end at %li and %li; ignore from %li",
	       (long) fr->firstframe, (long) fr->firstoff,
	       (long) fr->lastframe,  (long) fr->lastoff, (long) fr->ignoreframe);
#else
	debug3("frame_set_seek: begin at %li frames, end at %li; ignore from %li",
	       (long) fr->firstframe, (long) fr->lastframe, (long) fr->ignoreframe);
#endif
}

int attribute_align_arg mpg123_volume_change(mpg123_handle *mh, double change)
{
	if(mh == NULL) return MPG123_ERR;
	return mpg123_volume(mh, change + (double) mh->p.outscale);
}

int attribute_align_arg mpg123_volume(mpg123_handle *mh, double vol)
{
	if(mh == NULL) return MPG123_ERR;

	if(vol >= 0) mh->p.outscale = vol;
	else mh->p.outscale = 0.;

	do_rva(mh);
	return MPG123_OK;
}

static int get_rva(mpg123_handle *fr, double *peak, double *gain)
{
	double p = -1;
	double g = 0;
	int ret = 0;
	if(fr->p.rva)
	{
		int rt = 0;
		/* Should one assume a zero RVA as no RVA? */
		if(fr->p.rva == 2 && fr->rva.level[1] != -1) rt = 1;
		if(fr->rva.level[rt] != -1)
		{
			p = fr->rva.peak[rt];
			g = fr->rva.gain[rt];
			ret = 1; /* Success. */
		}
	}
	if(peak != NULL) *peak = p;
	if(gain != NULL) *gain = g;
	return ret;
}

/* adjust the volume, taking both fr->outscale and rva values into account */
void do_rva(mpg123_handle *fr)
{
	double peak = 0;
	double gain = 0;
	double newscale;
	double rvafact = 1;
	if(get_rva(fr, &peak, &gain))
	{
		if(NOQUIET && fr->p.verbose > 1) fprintf(stderr, "Note: doing RVA with gain %f\n", gain);
		rvafact = pow(10,gain/20);
	}

	newscale = fr->p.outscale*rvafact;

	/* if peak is unknown (== 0) this check won't hurt */
	if((peak*newscale) > 1.0)
	{
		newscale = 1.0/peak;
		warning2("limiting scale value to %f to prevent clipping with indicated peak factor of %f", newscale, peak);
	}
	/* first rva setting is forced with fr->lastscale < 0 */
	if(newscale != fr->lastscale || fr->decoder_change)
	{
		debug3("changing scale value from %f to %f (peak estimated to %f)", fr->lastscale != -1 ? fr->lastscale : fr->p.outscale, newscale, (double) (newscale*peak));
		fr->lastscale = newscale;
		/* It may be too early, actually. */
		if(fr->make_decode_tables != NULL) fr->make_decode_tables(fr); /* the actual work */
	}
}


int attribute_align_arg mpg123_getvolume(mpg123_handle *mh, double *base, double *really, double *rva_db)
{
	if(mh == NULL) return MPG123_ERR;
	if(base)   *base   = mh->p.outscale;
	if(really) *really = mh->lastscale;
	get_rva(mh, NULL, rva_db);
	return MPG123_OK;
}
