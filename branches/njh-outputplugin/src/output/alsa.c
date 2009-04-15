/*
	audio_alsa: sound output with Advanced Linux Sound Architecture 1.x API

	copyright 2006 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.de

	written by Clemens Ladisch <clemens@ladisch.de>
*/

#include "config.h"
#include "mpg123.h"
#include "audio.h"
#include "module.h"
#include "debug.h"

#include <errno.h>

/* make ALSA 0.9.x compatible to the 1.0.x API */
#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API

#include <alsa/asoundlib.h>

/* My laptop has probs playing low-sampled files with only 0.5s buffer... this should be a user setting -- ThOr */
#define BUFFER_LENGTH 0.5	/* in seconds */

static const struct {
	snd_pcm_format_t alsa;
	int mpg123;
} format_map[] = {
	{ SND_PCM_FORMAT_S16,    AUDIO_FORMAT_SIGNED_16   },
	{ SND_PCM_FORMAT_U16,    AUDIO_FORMAT_UNSIGNED_16 },
	{ SND_PCM_FORMAT_U8,     AUDIO_FORMAT_UNSIGNED_8  },
	{ SND_PCM_FORMAT_S8,     AUDIO_FORMAT_SIGNED_8    },
	{ SND_PCM_FORMAT_A_LAW,  AUDIO_FORMAT_ALAW_8      },
	{ SND_PCM_FORMAT_MU_LAW, AUDIO_FORMAT_ULAW_8      },
};
#define NUM_FORMATS (sizeof format_map / sizeof format_map[0])


static int rates_match(long int desired, unsigned int actual)
{
	return actual * 100 > desired * (100 - AUDIO_RATE_TOLERANCE) &&
	       actual * 100 < desired * (100 + AUDIO_RATE_TOLERANCE);
}

static int initialize_device(audio_output_t *ao)
{
	snd_pcm_hw_params_t *hw;
	int i;
	snd_pcm_format_t format;
	unsigned int rate;
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t period_size;
	snd_pcm_sw_params_t *sw;
	snd_pcm_uframes_t boundary;

	snd_pcm_hw_params_alloca(&hw);
	if (snd_pcm_hw_params_any(ao->userptr, hw) < 0) {
		error("initialize_device(): no configuration available");
		return -1;
	}
	if (snd_pcm_hw_params_set_access(ao->userptr, hw, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
		error("initialize_device(): device does not support interleaved access");
		return -1;
	}
	format = SND_PCM_FORMAT_UNKNOWN;
	for (i = 0; i < NUM_FORMATS; ++i) {
		if (ao->format == format_map[i].mpg123) {
			format = format_map[i].alsa;
			break;
		}
	}
	if (format == SND_PCM_FORMAT_UNKNOWN) {
		error1("initialize_device(): invalid sample format %d", ao->format);
		errno = EINVAL;
		return -1;
	}
	if (snd_pcm_hw_params_set_format(ao->userptr, hw, format) < 0) {
		error1("initialize_device(): cannot set format %s", snd_pcm_format_name(format));
		return -1;
	}
	if (snd_pcm_hw_params_set_channels(ao->userptr, hw, ao->channels) < 0) {
		error1("initialize_device(): cannot set %d channels", ao->channels);
		return -1;
	}
	rate = ao->rate;
	if (snd_pcm_hw_params_set_rate_near(ao->userptr, hw, &rate, NULL) < 0) {
		error1("initialize_device(): cannot set rate %u", rate);
		return -1;
	}
	if (!rates_match(ao->rate, rate)) {
		error2("initialize_device(): rate %ld not available, using %u", ao->rate, rate);
		/* return -1; */
	}
	buffer_size = rate * BUFFER_LENGTH;
	if (snd_pcm_hw_params_set_buffer_size_near(ao->userptr, hw, &buffer_size) < 0) {
		error("initialize_device(): cannot set buffer size");
		return -1;
	}
	period_size = buffer_size / 4;
	if (snd_pcm_hw_params_set_period_size_near(ao->userptr, hw, &period_size, NULL) < 0) {
		error("initialize_device(): cannot set period size");
		return -1;
	}
	if (snd_pcm_hw_params(ao->userptr, hw) < 0) {
		error("initialize_device(): cannot set hw params");
		return -1;
	}

	snd_pcm_sw_params_alloca(&sw);
	if (snd_pcm_sw_params_current(ao->userptr, sw) < 0) {
		error("initialize_device(): cannot get sw params");
		return -1;
	}
	/* start playing after the first write */
	if (snd_pcm_sw_params_set_start_threshold(ao->userptr, sw, 1) < 0) {
		error("initialize_device(): cannot set start threshold");
		return -1;
	}
	if (snd_pcm_sw_params_get_boundary(sw, &boundary) < 0) {
		error("initialize_device(): cannot get boundary");
		return -1;
	}
	#ifdef DEBUG
	{
		snd_pcm_uframes_t hw_buffer_size;
		snd_pcm_hw_params_get_buffer_size(hw, &hw_buffer_size);
		debug2("Alsa buffer_size %lu vs. boundary %lu", (unsigned long) hw_buffer_size, (unsigned long) boundary);
	}
	#endif
	/* never stop on underruns */
	if (snd_pcm_sw_params_set_stop_threshold(ao->userptr, sw, boundary) < 0) {
		error("initialize_device(): cannot set stop threshold");
		return -1;
	}
	/* wake up on every interrupt */
	if (snd_pcm_sw_params_set_avail_min(ao->userptr, sw, 1) < 0) {
		error("initialize_device(): cannot set min avail");
		return -1;
	}
	/* always write as many frames as possible */
	if (snd_pcm_sw_params_set_xfer_align(ao->userptr, sw, 1) < 0) {
		error("initialize_device(): cannot set transfer alignment");
		return -1;
	}
	/* play silence when there is an underrun */
	if (snd_pcm_sw_params_set_silence_size(ao->userptr, sw, boundary) < 0) {
		error("initialize_device(): cannot set silence size");
		return -1;
	}
	if (snd_pcm_sw_params(ao->userptr, sw) < 0) {
		error("initialize_device(): cannot set sw params");
		return -1;
	}
	return 0;
}


static int open_alsa(audio_output_t *ao)
{
	const char *pcm_name;
	snd_pcm_t *pcm;

	pcm_name = ao->device ? ao->device : "default";
	if (snd_pcm_open(&pcm, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
		error1("cannot open device %s", pcm_name);
		return -1;
	}
	ao->userptr = pcm;
	if (ao->format != -1) {
		/* we're going to play: initalize sample format */
		return initialize_device(ao);
	} else {
		/* query mode; sample format will be set for each query */
		return 0;
	}
}


static int get_formats_alsa(audio_output_t *ao)
{
	snd_pcm_hw_params_t *hw;
	unsigned int rate;
	int supported_formats, i;

	snd_pcm_hw_params_alloca(&hw);
	if (snd_pcm_hw_params_any(ao->userptr, hw) < 0) {
		error("no configuration available");
		return -1;
	}
	if (snd_pcm_hw_params_set_access(ao->userptr, hw, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
		return -1;
	if (snd_pcm_hw_params_set_channels(ao->userptr, hw, ao->channels) < 0)
		return 0;
	rate = ao->rate;
	if (snd_pcm_hw_params_set_rate_near(ao->userptr, hw, &rate, NULL) < 0)
		return -1;
	if (!rates_match(ao->rate, rate))
		return 0;
	supported_formats = 0;
	for (i = 0; i < NUM_FORMATS; ++i) {
		if (snd_pcm_hw_params_test_format(ao->userptr, hw, format_map[i].alsa) == 0)
			supported_formats |= format_map[i].mpg123;
	}
	return supported_formats;
}

static int write_alsa(audio_output_t *ao, unsigned char *buf, int bytes)
{
	snd_pcm_uframes_t frames;
	snd_pcm_sframes_t written;

#if SND_LIB_VERSION >= 0x000901
	snd_pcm_sframes_t delay;
	if (snd_pcm_delay(ao->userptr, &delay) >= 0 && delay < 0)
		/* underrun - move the application pointer forward to catch up */
		snd_pcm_forward(ao->userptr, -delay);
#endif
	frames = snd_pcm_bytes_to_frames(ao->userptr, bytes);
	written = snd_pcm_writei(ao->userptr, buf, frames);
	if (written >= 0)
		return snd_pcm_frames_to_bytes(ao->userptr, written);
	else
		return written;
}

static void flush_alsa(audio_output_t *ao)
{
	/* is this the optimal solution? - we should figure out what we really whant from this function */
	snd_pcm_drop(ao->userptr);
	snd_pcm_prepare(ao->userptr);
}

static int close_alsa(audio_output_t *ao)
{
	if(ao->userptr != NULL) /* be really generous for being called without any device opening */
	{
		if (snd_pcm_state(ao->userptr) == SND_PCM_STATE_RUNNING)
			snd_pcm_drain(ao->userptr);
		return snd_pcm_close(ao->userptr);
	}
	else return 0;
}


static int init_alsa(audio_output_t* ao)
{
	if (ao==NULL) return -1;

	/* Set callbacks */
	ao->open = open_alsa;
	ao->flush = flush_alsa;
	ao->write = write_alsa;
	ao->get_formats = get_formats_alsa;
	ao->close = close_alsa;

	/* Success */
	return 0;
}



/* 
	Module information data structure
*/
mpg123_module_t mpg123_output_module_info = {
	/* api_version */	MPG123_MODULE_API_VERSION,
	/* name */			"alsa",						
	/* description */	"Output audio using Advanced Linux Sound Architecture (ALSA).",
	/* revision */		"$Rev:$",						
	/* handle */		NULL,
	
	/* init_output */	init_alsa,						
};
