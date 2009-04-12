
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "audio.h"

int audio_open(struct audio_info_struct *ai)
{
  ai->config = ALnewconfig();

  if(ai->channels == 2)
    ALsetchannels(ai->config, AL_STEREO);
  else
    ALsetchannels(ai->config, AL_MONO);
  ALsetwidth(ai->config, AL_SAMPLE_16);

  ALsetqueuesize(ai->config, 131069);
    
  ai->port = ALopenport("mpg132", "w", ai->config);
  if(ai->port == NULL){
    fprintf(stderr, "Unable to open audio channel.");
    exit(-1);
  }

  audio_reset_parameters(ai);
    
  return 1;
}

int audio_reset_parameters(struct audio_info_struct *ai)
{
  int ret;
  ret = audio_set_format(ai);
  if(ret >= 0)
    ret = audio_set_channels(ai);
  if(ret >= 0)
    ret = audio_set_rate(ai);

/* todo: Set new parameters here */

  return ret;
}

int audio_rate_best_match(struct audio_info_struct *ai)
{
  return 0;
}

int audio_set_rate(struct audio_info_struct *ai)
{
    long params[2] = {AL_OUTPUT_RATE, 44100};

    params[1] = ai->rate;
    ALsetparams(AL_DEFAULT_DEVICE, params, 2);
    return 0;
}

int audio_set_channels(struct audio_info_struct *ai)
{
    if(ai->channels == 2)
      ALsetchannels(ai->config, AL_STEREO);
    else
      ALsetchannels(ai->config, AL_MONO);
    return 0;
}

int audio_set_format(struct audio_info_struct *ai)
{
  return 0;
}

int audio_get_formats(struct audio_info_struct *ai)
{
  return AUDIO_FORMAT_SIGNED_16;
}


int audio_play_samples(struct audio_info_struct *ai,unsigned char *buf,int len)
{
  if(ai->format == AUDIO_FORMAT_SIGNED_8)
    ALwritesamps(ai->port, buf, len);
  else
    ALwritesamps(ai->port, buf, len>>1);
  return len;
}

int audio_close(struct audio_info_struct *ai)
{
    while(ALgetfilled(ai->port) > 0)
	sginap(1);  
    ALcloseport(ai->port);
    ALfreeconfig(ai->config);
    return 0;
}