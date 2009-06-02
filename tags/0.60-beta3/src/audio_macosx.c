/*
	audio_macosx: audio output on MacOS X

	copyright ?-2006 by the mpg123 project - free software under the terms of the GPL 2
	see COPYING and AUTHORS files in distribution or http://mpg123.de
	initially written by Guillaume Outters
*/

/* audio_macosx.c, originally written by Guillaume Outters
 * to contact the author, please mail to: guillaume.outters@free.fr
 *
 * This file is some quick pre-alpha patch to allow me to have some music for my
 * long working days, and it does it well. But it surely isn't a final version;
 * as Mac OS X requires at least a G3, I'm not sure it will be useful to
 * implement downsampling.
 *
 * Mac OS X audio works by asking you to fill its buffer, and, to complicate a
 * bit, you must provide it with floats. In order not to patch too much mpg123,
 * we'll accept signed short (mpg123 "approved" format) and transform them into
 * floats as soon as received. Let's say this way calculations are faster.
 *
 * As we don't have some /dev/audio device with blocking write, we'll have to
 * stop mpg123 before it does too much work, while we are waiting our dump proc
 * to be called. I wanted to use semaphores, but they still need an
 * implementation from Apple before I can do anything. So we'll block using a
 * sleep and wake up on SIGUSR2.
 * Version 0.2: now I use named semaphores (which are implemented AND work).
 * Preprocessor flag MOSX_USES_SEM (defined at the beginning of this file)
 * enables this behaviour.
 *
 * In order always to have a ready buffer to be dumped when the routine gets
 * called, we have a "buffer loop" of NUMBER_BUFFERS buffers. mpg123 fills it
 * on one extremity ('to'), playProc reads it on another point ('from'). 'to'
 * blocks when it arrives on 'from' (having filled the whole circle of buffers)
 * and 'from' blocks when no data is available. As soon as it has emptied a
 * buffer, if mpg123 is sleeping, it awakes it quite brutaly (SIGUSR2) to tell
 * it to fill the buffer. */

#ifndef MOSX_USES_SEM
#define MOSX_USES_SEM 1	/* Semaphores or sleep()/kill()? I would say semaphores, but this is just my advice, after all */
#endif
#ifndef MOSX_SEM_V2
#define MOSX_SEM_V2 1
#endif

#include "config.h"
#include "mpg123.h"
#include <CoreAudio/AudioHardware.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#if MOSX_USES_SEM
#include <semaphore.h>
#endif

struct aBuffer
{
	float * buffer;
	long size;
	
	float * ptr;	/* Where in the buffer are we? */
	long remaining;
	
	struct aBuffer * next;
};
typedef struct aBuffer aBuffer;

struct anEnv
{
	long size;
	short * debut;
	short * ptr;
	AudioDeviceID device;
	char play;
	
	/* Intermediate buffers */
	
	#if MOSX_USES_SEM
	sem_t * semaphore;
	#else
	char wait;	/* mpg123 is waiting (due to the semaphore) to show the world its power; let's free him! */
	pid_t pid;
	#endif
	aBuffer * from;	/* Current buffers */
	aBuffer * to;
};

static struct anEnv env;

#define ENV ((struct anEnv *)inClientData)
#define NUMBER_BUFFERS 16	/* Tried with 3 buffers, but then any little window move is sufficient to stop the sound. Here we have 1.5 seconds music buffered */

void destroyBuffers()
{
	aBuffer * ptr;
	aBuffer * ptr2;
	
	ptr = env.to->next;
	env.to->next = NULL;
	while(ptr)
	{
		ptr2 = ptr->next;
		if(ptr->buffer) free(ptr->buffer);
		free(ptr);
		ptr = ptr2;
	}
}

void initBuffers()
{
	long m;
	aBuffer ** ptrptr;
	
	ptrptr = &env.to;
	for(m = 0; m < NUMBER_BUFFERS; m++)
	{
		*ptrptr = malloc(sizeof(aBuffer));
		(*ptrptr)->size = 0;
		(*ptrptr)->remaining = 0;
		(*ptrptr)->buffer = NULL;
		ptrptr = &(*ptrptr)->next;
		#if MOSX_USES_SEM
		sem_post(env.semaphore);	/* This buffer is ready for filling (of course, it is empty!) */ 
		#endif
	}
	*ptrptr = env.from = env.to;
}

int fillBuffer(aBuffer * b, short * source, long size)
{
	float * dest;
	
	if(b->remaining)	/* Non empty buffer, must still be playing */
		return(-1);
	if(b->size != size)	/* Hey! What's that? Coudn't this buffer size be fixed once (well, perhaps we just didn't allocate it yet) */
	{
		if(b->buffer) free(b->buffer);
		b->buffer = malloc(size * sizeof(float));
		b->size = size;
	}
	
	dest = b->buffer;
	while(size--)
		*dest++ = (*source++) / 32768.0;
	
	b->ptr = b->buffer;
	b->remaining = b->size;	/* Do this at last; we shouldn't show the buffer is full before it is effectively */
	
	#ifdef DEBUG_MOSX
	printf("."); fflush(stdout);
	#endif
	
	return(0);
}

OSStatus playProc(AudioDeviceID inDevice, const AudioTimeStamp * inNow, const AudioBufferList * inInputData, const AudioTimeStamp * inInputTime, AudioBufferList * outOutputData, const AudioTimeStamp * inOutputTime, void * inClientData)
{
	long m, n, o;
	float * dest;
	
	for(o = 0; o < outOutputData->mNumberBuffers; o++)
	{
		m = outOutputData->mBuffers[o].mDataByteSize / sizeof(float);	/* What we have to fill */
		dest = outOutputData->mBuffers[o].mData;
		
		while(m > 0)
		{
			if( (n = ENV->from->remaining) <= 0 )	/* No more bytes in the current read buffer! */
			{
				while( (n = ENV->from->remaining) <= 0)
					usleep(2000);	/* Let's wait a bit for the results... */
			}
						
			/* We dump what we can */
			
			if(n > m) n = m;	/* In fact, just the necessary should be sufficient (I think) */
			
			memcpy(dest, ENV->from->ptr, n * sizeof(float));
			
			/* Let's remember all done work */
			
			m -= n;
			ENV->from->ptr += n;
			if( (ENV->from->remaining -= n) <= 0)	/* ... and tell mpg123 there's a buffer to fill */
			{
				#if MOSX_USES_SEM
				sem_post(ENV->semaphore);
				#else
				if(ENV->wait)
				{
					kill(ENV->pid, SIGUSR2);
					ENV->wait = 0;
				}
				#endif
				ENV->from = ENV->from->next;
			}
		}
	}
	
	return (0); 
}

#if ! MOSX_USES_SEM
void start(int n)
{
	signal(SIGUSR2, start);
}
#endif

int audio_open(struct audio_info_struct *ai)
{
	UInt32 size;
	AudioStreamBasicDescription format;
	#if MOSX_USES_SEM
	char s[10];
	long m;
	#endif
	/*float vol;
	OSStatus e;*/
	
	/* Where did that default audio output go? */
	
	size = sizeof(env.device);
	if(AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice, &size, &env.device)) return(-1);
		
	/* Hmmm, let's choose PCM format */
	
	size = sizeof(format);
	if(AudioDeviceGetProperty(env.device, 0, 0, kAudioDevicePropertyStreamFormat, &size, &format)) return(-1);
	if(format.mFormatID != kAudioFormatLinearPCM) return(-1);
	
	/* Let's test volume, which doesn't seem to work (but this is only an alpha, remember?) */
	
	/*vol = 0.5;
	size = sizeof(vol);
	if(e = AudioDeviceSetProperty(env.device, NULL, 0, 0, 'volm', size, &vol)) { printf("Didn't your mother ever tell you not to hear music so loudly?\n"); return(-1); } */
	
	/* Let's init our environment */
	
	env.size = 0;
	env.debut = NULL;
	env.ptr = NULL;
	env.play = 0;
	
	#if MOSX_USES_SEM
	strcpy(s, "/mpg123-0000");
	do
	{
		for(m = 10;; m--)
			if( (s[m]++) <= '9')
				break;
			else
				s[m] = '0';
	} while( (env.semaphore = sem_open(s, O_CREAT | O_EXCL, 0644, 0)) == (sem_t *)SEM_FAILED);
	#else
	env.pid = getpid();
	env.wait = 0;
	signal(SIGUSR2, start);
	#endif
	
	initBuffers();
		
	/* And prepare audio launching */
	
	if(AudioDeviceAddIOProc(env.device, playProc, &env)) return(-1);
	
	return(0);
}

int audio_get_formats(struct audio_info_struct *ai)
{
	return AUDIO_FORMAT_SIGNED_16;
}

int audio_play_samples(struct audio_info_struct *ai,unsigned char *buf,int len)
{
	/* We have to calm down mpg123, else he wouldn't hesitate to drop us another buffer (which would be the same, in fact) */
	
	#if MOSX_USES_SEM && MOSX_SEM_V2	/* Suddenly, I have some kind of doubt: HOW COULD IT WORK??? */
	while(sem_wait(env.semaphore)){}	/* We just have to wait a buffer fill request */
	fillBuffer(env.to, (short *)buf, len / sizeof(short));
	#else
	while(fillBuffer(env.to, (short *)buf, len / sizeof(short)) < 0)	/* While the next buffer to write is not empty, we wait a bit... */
	{
		#ifdef DEBUG_MOSX
		printf("|"); fflush(stdout);
		#endif
		#if MOSX_USES_SEM
		sem_wait(env.semaphore);	/* This is a bug. I should wait for the semaphore once per empty buffer (and not each time fillBuffers() returns -1). See MOSX_SEM_V2; this was a too quickly modified part when I implemented MOSX_USES_SEM. */
		#else
		env.wait = 1;
		sleep(3600);	/* This should be sufficient, shouldn't it? */
		#endif
	}
	#endif
	env.to = env.to->next;
	
	/* And we lauch action if not already done */
	
	if(!env.play)
	{
		if(AudioDeviceStart(env.device, playProc)) return(-1);
		env.play = 1;
	}
	
	return len;
}

int audio_close(struct audio_info_struct *ai)
{
	AudioDeviceStop(env.device, playProc);	/* No matter the error code, we want to close it (by brute force if necessary) */
	AudioDeviceRemoveIOProc(env.device, playProc);
	destroyBuffers();
	#if MOSX_USES_SEM
	sem_close(env.semaphore);
	#endif
	return 0;
}

void audio_queueflush(struct audio_info_struct *ai)
{
}