/* 
 *  simple audio Lib .. 
 *  copyrights (c) 1994,1995,1996 by Michael Hipp
 *  SGI audio support copyrights (c) 1995 by Thomas Woerner
 *  copy policy: GPL V2.0
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef SOLARIS
#include <stropts.h>
#include <sys/conf.h>
#endif

#include "mpg123.h"

#ifdef VOXWARE
#include <sys/ioctl.h>
#ifdef LINUX
#include <linux/soundcard.h>
#else
#include <machine/soundcard.h>
#endif

int audio_open(struct audio_info_struct *ai)
{
  if(!ai)
    return -1;

  if(ai->rate == -1)
    ai->rate = 44100;

  if(!ai->device)
    ai->device = "/dev/dsp";

  ai->fn = open(ai->device,O_WRONLY);  

  if(ai->fn < 0)
  {
    fprintf(stderr,"Can't open %s!\n",ai->device);
    exit(1);
  }

  if(audio_reset_parameters(ai) < 0) {
    close(ai->fn);
    return -1;
  }

  if(ai->gain >= 0) {
    int e,mask;
    e = ioctl(ai->fn , SOUND_MIXER_READ_DEVMASK ,&mask);
    if(e < 0) {
      fprintf(stderr,"audio/gain: Can't get audio device features list.\n");
    }
    else if(mask & SOUND_MASK_PCM) {
      int gain = (ai->gain<<8)|(ai->gain);
      e = ioctl(ai->fn, SOUND_MIXER_WRITE_PCM , &gain);
    }
    else if(!(mask & SOUND_MASK_VOLUME)) {
      fprintf(stderr,"audio/gain: setable Volume/PCM-Level not supported by your audio device: %#04x\n",mask);
    }
    else { 
      int gain = (ai->gain<<8)|(ai->gain);
      e = ioctl(ai->fn, SOUND_MIXER_WRITE_VOLUME , &gain);
    }
  }

  return ai->fn;
}

int audio_reset_parameters(struct audio_info_struct *ai)
{
  int ret;
  ret = ioctl(ai->fn,SNDCTL_DSP_RESET,NULL);
  if(ret >= 0)
    ret = audio_set_format(ai);
  if(ret >= 0)
    ret = audio_set_channels(ai);
  if(ret >= 0)
    ret = audio_set_rate(ai);
  return ret;
}

int audio_set_rate(struct audio_info_struct *ai)
{
  int dsp_samplesize;

  if(ai->rate >= 0)
  {
    dsp_samplesize = ai->rate;
    return ioctl(ai->fn, SNDCTL_DSP_SPEED,&dsp_samplesize);
  }
  return 0;
}

int audio_set_channels(struct audio_info_struct *ai)
{
  int chan = ai->channels - 1;
  return ioctl(ai->fn, SNDCTL_DSP_STEREO, &chan);
}

int audio_set_format(struct audio_info_struct *ai)
{
  int sample_size,fmts;

  switch(ai->format) {
    case -1:
    case AUDIO_FORMAT_SIGNED_16:
    default:
      fmts = AFMT_S16_LE;
      sample_size = 16;
      break;
    case AUDIO_FORMAT_UNSIGNED_8:
      fmts = AFMT_U8;
      sample_size = 8;
      break;
    case AUDIO_FORMAT_SIGNED_8:
      fmts = AFMT_S8;
      sample_size = 8;
      break;
    case AUDIO_FORMAT_ULAW_8:
      fmts = AFMT_MU_LAW;
      sample_size = 8;
      break;
  }
  if(ioctl(ai->fn, SNDCTL_DSP_SAMPLESIZE,&sample_size) < 0)
    return -1;
  return ioctl(ai->fn, SNDCTL_DSP_SETFMT, &fmts);
}

int audio_get_formats(struct audio_info_struct *ai)
{
  int ret = 0;
  int fmts;

  if(ioctl(ai->fn,SNDCTL_DSP_GETFMTS,&fmts) < 0)
    return -1;

  if(fmts & AFMT_MU_LAW)
    ret |= AUDIO_FORMAT_ULAW_8;
  if(fmts & AFMT_S16_LE)
    ret |= AUDIO_FORMAT_SIGNED_16;
  if(fmts & AFMT_U8)
    ret |= AUDIO_FORMAT_UNSIGNED_8;
  if(fmts & AFMT_S8)
    ret |= AUDIO_FORMAT_SIGNED_8;

  return ret;
}


int audio_play_samples(struct audio_info_struct *ai,unsigned char *buf,int len)
{
  return write(ai->fn,buf,len);
}

int audio_close(struct audio_info_struct *ai)
{
  close (ai->fn);
  return 0;
}

#elif defined(SOLARIS) || defined(SUNOS)

#include <sys/filio.h>
#ifdef SUNOS
#include <sun/audioio.h>
#else
#include <sys/audioio.h>
#endif

int audio_open(struct audio_info_struct *ai)
{
  audio_info_t ainfo;

  ai->fn = open("/dev/audio",O_WRONLY);
  if(ai->fn < 0)
     return ai->fn;

#ifdef SUNOS
  {
    int type;
    if(ioctl(ai->fn, AUDIO_GETDEV, &type) == -1)
      return -1;
    if(type == AUDIO_DEV_UNKNOWN || type == AUDIO_DEV_AMD)
      return -1;
  }
#else
#if 0
  {
    struct audio_device ad;
    if(ioctl(ai->fn, AUDIO_GETDEV, &ad) == -1)
      return -1;
fprintf(stderr,"%s\n",ad.name);
    if(strstr(ad.name,"CS4231"))
      fprintf(stderr,"Warning: Your machine can't play full 44.1Khz stereo.\n");
    if(!strstr(ad.name,"dbri") && !strstr(ad.name,"CS4231"))
      fprintf(stderr,"Warning: Unknown sound system %s. But we try it.\n",ad.name);
  }
#endif
#endif

  if(audio_reset_parameters(ai) < 0) {
    return -1;
  }

  if(ioctl(ai->fn, AUDIO_GETINFO, &ainfo) == -1)
    return -1;

  switch(ai->output)
  {
    case AUDIO_OUT_INTERNAL_SPEAKER:
      ainfo.play.port = AUDIO_SPEAKER;
      break;
    case AUDIO_OUT_HEADPHONES:
      ainfo.play.port = AUDIO_HEADPHONE;
      break;
    case AUDIO_OUT_LINE_OUT:
      ainfo.play.port = AUDIO_LINE_OUT;
      break;
  }

  if(ai->gain != -1)
    ainfo.play.gain = ai->gain;

  if(ioctl(ai->fn, AUDIO_SETINFO, &ainfo) == -1)
    return -1;

  return ai->fn;
}

int audio_reset_parameters(struct audio_info_struct *ai)
{
  int ret;
  ret = audio_set_format(ai);
  if(ret >= 0)
    ret = audio_set_channels(ai);
  if(ret >= 0)
    ret = audio_set_rate(ai);
  return ret;
}

int audio_set_rate(struct audio_info_struct *ai)
{
  audio_info_t ainfo;

  if(ai->rate != -1)
  {
    if(ioctl(ai->fn, AUDIO_GETINFO, &ainfo) == -1)
      return -1;
    ainfo.play.sample_rate = ai->rate;
    if(ioctl(ai->fn, AUDIO_SETINFO, &ainfo) == -1)
      return -1;
    return 0;
  }
  return -1;
}

int audio_set_channels(struct audio_info_struct *ai)
{
  audio_info_t ainfo;

  if(ioctl(ai->fn, AUDIO_GETINFO, &ainfo) == -1)
    return -1;
  ainfo.play.channels = ai->channels;
  if(ioctl(ai->fn, AUDIO_SETINFO, &ainfo) == -1)
    return -1;
  return 0;
}

int audio_set_format(struct audio_info_struct *ai)
{
  audio_info_t ainfo;

  if(ioctl(ai->fn, AUDIO_GETINFO, &ainfo) == -1)
    return -1;

  switch(ai->format) {
    case -1:
    case AUDIO_FORMAT_SIGNED_16:
    default:
      ainfo.play.encoding = AUDIO_ENCODING_LINEAR;
      ainfo.play.precision = 16;
      break;
    case AUDIO_FORMAT_UNSIGNED_8:
      ainfo.play.encoding = AUDIO_ENCODING_LINEAR8;
      ainfo.play.precision = 8;
      break;
    case AUDIO_FORMAT_SIGNED_8:
      fprintf(stderr,"Linear signed 8 bit not supported!\n");
      return -1;
    case AUDIO_FORMAT_ULAW_8:
      ainfo.play.encoding = AUDIO_ENCODING_ULAW;
      ainfo.play.precision = 8;
      break;
    case AUDIO_FORMAT_ALAW_8:
      ainfo.play.encoding = AUDIO_ENCODING_ALAW;
      ainfo.play.precision = 8;
      break;
  }

  if(ioctl(ai->fn, AUDIO_SETINFO, &ainfo) == -1)
    return -1;

  return 0;
}

int audio_get_formats(struct audio_info_struct *ai)
{
  return AUDIO_FORMAT_SIGNED_16|AUDIO_FORMAT_ULAW_8;
}

int audio_play_samples(struct audio_info_struct *ai,unsigned char *buf,int len)
{
  return write(ai->fn,buf,len);
}

int audio_close(struct audio_info_struct *ai)
{
  close (ai->fn);
  return 0;
}

#ifdef SOLARIS
void audio_queueflush (struct audio_info_struct *ai)
{
	ioctl (ai->fn, I_FLUSH, FLUSHRW);
}
#endif


#elif defined(HPUX)
#include <sys/audio.h>

int audio_open(struct audio_info_struct *ai)
{
  struct audio_describe ades;
  struct audio_gain again;
  int i,audio;

  ai->fn = open("/dev/audio",O_RDWR);

  if(ai->fn < 0)
    return -1;


  ioctl(ai->fn,AUDIO_DESCRIBE,&ades);

  if(ai->gain != -1)
  {
     if(ai->gain > ades.max_transmit_gain)
     {
       fprintf(stderr,"your gainvalue was to high -> set to maximum.\n");
       ai->gain = ades.max_transmit_gain;
     }
     if(ai->gain < ades.min_transmit_gain)
     {
       fprintf(stderr,"your gainvalue was to low -> set to minimum.\n");
       ai->gain = ades.min_transmit_gain;
     }
     again.channel_mask = AUDIO_CHANNEL_0 | AUDIO_CHANNEL_1;
     ioctl(ai->fn,AUDIO_GET_GAINS,&again);
     again.cgain[0].transmit_gain = ai->gain;
     again.cgain[1].transmit_gain = ai->gain;
     again.channel_mask = AUDIO_CHANNEL_0 | AUDIO_CHANNEL_1;
     ioctl(ai->fn,AUDIO_SET_GAINS,&again);
  }
  
  if(ai->output != -1)
  {
     if(ai->output == AUDIO_OUT_INTERNAL_SPEAKER)
       ioctl(ai->fn,AUDIO_SET_OUTPUT,AUDIO_OUT_SPEAKER);
     else if(ai->output == AUDIO_OUT_HEADPHONES)
       ioctl(ai->fn,AUDIO_SET_OUTPUT,AUDIO_OUT_HEADPHONE);
     else if(ai->output == AUDIO_OUT_LINE_OUT)
       ioctl(ai->fn,AUDIO_SET_OUTPUT,AUDIO_OUT_LINE);
  }
  
  if(ai->rate == -1)
    ai->rate = 44100;

  for(i=0;i<ades.nrates;i++)
  {
    if(ai->rate == ades.sample_rate[i])
      break;
  }
  if(i == ades.nrates)
  {
    fprintf(stderr,"Can't set sample-rate to %ld.\n",ai->rate);
    i = 0;
  }

  if(audio_reset_parameters(ai) < 0)
    return -1;
 
  return ai->fn;
}

int audio_reset_parameters(struct audio_info_struct *ai)
{
  int ret;
  ret = audio_set_format(ai);
  if(ret >= 0)
    ret = audio_set_channels(ai);
  if(ret >= 0)
    ret = audio_set_rate(ai);
  return ret;
}

int audio_set_rate(struct audio_info_struct *ai)
{
  if(ai->rate >= 0)
    return ioctl(ai->fn,AUDIO_SET_SAMPLE_RATE,ai->rate);
  return 0;
}

int audio_set_channels(struct audio_info_struct *ai)
{
  return ioctl(ai->fn,AUDIO_SET_CHANNELS,ai->channels);
}

int audio_set_format(struct audio_info_struct *ai)
{
  int fmt;

  switch(ai->format) {
    case -1:
    case AUDIO_FORMAT_SIGNED_16:
    default: 
      fmt = AUDIO_FORMAT_LINEAR16BIT;
      break;
    case AUDIO_FORMAT_UNSIGNED_8:
      fprintf(stderr,"unsigned 8 bit linear not supported\n");
      return -1;
    case AUDIO_FORMAT_SIGNED_8:
      fprintf(stderr,"signed 8 bit linear not supported\n");
      return -1;
    case AUDIO_FORMAT_ALAW_8:
      fmt = AUDIO_FORMAT_ALAW;
      break;
    case AUDIO_FORMAT_ULAW_8:
      fmt = AUDIO_FORMAT_ULAW;
      break;
  }
  return ioctl(ai->fn,AUDIO_SET_DATA_FORMAT,fmt);
}

int audio_get_formats(struct audio_info_struct *ai)
{
  return AUDIO_FORMAT_SIGNED_16;
}


int audio_play_samples(struct audio_info_struct *ai,unsigned char *buf,int len)
{
  return write(ai->fn,buf,len);
}

int audio_close(struct audio_info_struct *ai)
{
  close (ai->fn);
  return 0;
}

#elif defined(SGI)

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

int audio_set_rate(struct audio_info_struct *ai)
{
    long params[2] = {AL_OUTPUT_RATE, 44100};

    params[1] = ai->rate;
    ALsetparams(AL_DEFAULT_DEVICE, params, 2);    
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
    return ALwritesamps(ai->port, buf, len);
  else
    return ALwritesamps(ai->port, buf, len>>1)*2;
}

int audio_close(struct audio_info_struct *ai)
{
    while(ALgetfilled(ai->port) > 0)
	sginap(1);  
    ALcloseport(ai->port);
    ALfreeconfig(ai->config);
}

#else
int audio_open(struct audio_info_struct *ai)
{
  fprintf(stderr,"No audio device support compiled into this binary (use -s).\n");
  return -1;
}

int audio_reset_parameters(struct audio_info_struct *ai)
{
  return 0;
}

int audio_set_rate(struct audio_info_struct *ai)
{
  return 0;
}

int audio_set_channels(struct audio_info_struct *ai)
{
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
  return len;
}

int audio_close(struct audio_info_struct *ai)
{
  return 0;
}
#endif


