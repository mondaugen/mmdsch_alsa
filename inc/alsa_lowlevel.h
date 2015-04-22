#ifndef ALSA_LOWLEVEL_H
#define ALSA_LOWLEVEL_H

#include <alsa/asoundlib.h> 
#include <stdint.h>

typedef audio_hw_sample_t int16_t;

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
} audio_hw_setup_t;

typedef audio_hw_err_t int;

#endif /* ALSA_LOWLEVEL_H */
