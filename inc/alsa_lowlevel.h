#ifndef ALSA_LOWLEVEL_H
#define ALSA_LOWLEVEL_H

#include <alsa/asoundlib.h> 
#include <stdint.h>

#define AUDIO_HW_SAMPLE_T_MAX 32767 

typedef int16_t audio_hw_sample_t;
typedef int     audio_hw_err_t;

typedef struct {
    char *device;			    /* playback device */
    snd_pcm_format_t format;	/* sample format */
    unsigned int rate;			/* stream rate */
    unsigned int channels;		/* count of channels */
    unsigned int buffer_time;	/* ring buffer length in us */
    unsigned int period_time;	/* period time in us */
    int verbose;				/* verbose flag */
    int resample;				/* enable alsa-lib resampling */
    int period_event;			/* produce poll event after each period */
    char  *method;              /* the transfer method. Can only be "async" for now. */
} audio_hw_setup_t;

#endif /* ALSA_LOWLEVEL_H */
