/* Implementation based on test/pcm.c found in the alsa-lib repository. */

#include "alsa_lowlevel.h" 
#include "audio_hw.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <sys/time.h>
#include <math.h>

static char *device = "plughw:0,0";			/* playback device */
static snd_pcm_format_t format = SND_PCM_FORMAT_S16;	/* sample format */
static unsigned int rate = 44100;			/* stream rate */
static unsigned int channels = 1;			/* count of channels */
static unsigned int buffer_time = 500000;	/* ring buffer length in us */
static unsigned int period_time = 100000;	/* period time in us */
static double freq = 440;				    /* sinusoidal wave frequency in Hz */
static int verbose = 0;					    /* verbose flag */
static int resample = 1;				    /* enable alsa-lib resampling */
static int period_event = 0;				/* produce poll event after each period */

static snd_pcm_sframes_t buffer_size;
static snd_pcm_sframes_t period_size;
static snd_output_t *output = NULL;

struct async_private_data {
	signed short *samples;
	snd_pcm_channel_area_t *areas;
	double phase;
};

static struct async_private_data async_data;
static snd_async_handler_t *async_ahandler;

/* The audio_hw_io_t structure */
audio_hw_io_t audiohwio;

/* This will be redefined to call a user defined callback to fill the areas. */
static void generate_sine(const snd_pcm_channel_area_t *areas, 
        snd_pcm_uframes_t offset,
        int count, double *_phase)
{
    static double max_phase = 2. * M_PI;
    double phase = *_phase;
    double step = max_phase*freq/(double)rate;
    unsigned char *samples[channels];
    int steps[channels];
    unsigned int chn;
    int format_bits = snd_pcm_format_width(format);
    unsigned int maxval = (1 << (format_bits - 1)) - 1;
    int bps = format_bits / 8;  /* bytes per sample */
    int phys_bps = snd_pcm_format_physical_width(format) / 8;
    int big_endian = snd_pcm_format_big_endian(format) == 1;
    int to_unsigned = snd_pcm_format_unsigned(format) == 1;
    int is_float = (format == SND_PCM_FORMAT_FLOAT_LE ||
            format == SND_PCM_FORMAT_FLOAT_BE);

    /* verify and prepare the contents of areas */
    for (chn = 0; chn < channels; chn++) {
        if ((areas[chn].first % 8) != 0) {
            printf("areas[%i].first == %i, aborting...\n", chn, areas[chn].first);
            exit(EXIT_FAILURE);
        }
        samples[chn] = /*(signed short *)*/(((unsigned char *)areas[chn].addr) + (areas[chn].first / 8));
        if ((areas[chn].step % 16) != 0) {
            printf("areas[%i].step == %i, aborting...\n", chn, areas[chn].step);
            exit(EXIT_FAILURE);
        }
        steps[chn] = areas[chn].step / 8;
        samples[chn] += offset * steps[chn];
    }
    /* Set the number of frames to fill, I guess this might not always be the
     * period_size */
    audiohwio.length = count;

    /* *** We do not yet read samples in *** */

    /* Fill the audio_hw_io_t struct with samples */
    audio_hw_io(&audiohwio);
    /* fill the channel areas */
    for (chn = 0; chn < channels; chn++) {
        count = audiohwio.length;
        while (count-- > 0) {
            union {
                float f;
                int i;
            } fval;
            int res, i;
            if (is_float) {
                fval.f = audiohwio.out[(audiohwio.length - count - 1)
                    * audiohwio.nchans_out + chn];
                res = fval.i;
            } else {
                res = audiohwio.out[(audiohwio.length - count - 1)
                    * audiohwio.nchans_out + chn];
            }
            if (to_unsigned) {
                res ^= 1U << (format_bits - 1);
            }
            /* Generate data in native endian format */
            if (big_endian) {
                for (i = 0; i < bps; i++)
                    *(samples[chn] + phys_bps - 1 - i) = (res >> i * 8) & 0xff;
            } else {
                for (i = 0; i < bps; i++)
                    *(samples[chn] + i) = (res >>  i * 8) & 0xff;
            }
            samples[chn] += steps[chn];
        }
    }
}

static int set_hwparams(snd_pcm_t *handle,
			snd_pcm_hw_params_t *params,
			snd_pcm_access_t access)
{
	unsigned int rrate;
	snd_pcm_uframes_t size;
	int err, dir;

	/* choose all parameters */
	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0) {
		printf("Broken configuration for playback: no configurations available: %s\n", snd_strerror(err));
		return err;
	}
	/* set hardware resampling */
	err = snd_pcm_hw_params_set_rate_resample(handle, params, resample);
	if (err < 0) {
		printf("Resampling setup failed for playback: %s\n", snd_strerror(err));
		return err;
	}
	/* set the interleaved read/write format */
	err = snd_pcm_hw_params_set_access(handle, params, access);
	if (err < 0) {
		printf("Access type not available for playback: %s\n", snd_strerror(err));
		return err;
	}
	/* set the sample format */
	err = snd_pcm_hw_params_set_format(handle, params, format);
	if (err < 0) {
		printf("Sample format not available for playback: %s\n", snd_strerror(err));
		return err;
	}
	/* set the count of channels */
	err = snd_pcm_hw_params_set_channels(handle, params, channels);
	if (err < 0) {
		printf("Channels count (%i) not available for playbacks: %s\n", channels, snd_strerror(err));
		return err;
	}
	/* set the stream rate */
	rrate = rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
	if (err < 0) {
		printf("Rate %iHz not available for playback: %s\n", rate, snd_strerror(err));
		return err;
	}
	if (rrate != rate) {
		printf("Rate doesn't match (requested %iHz, get %iHz)\n", rate, err);
		return -EINVAL;
	}
	/* set the buffer time */
	err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, &dir);
	if (err < 0) {
		printf("Unable to set buffer time %i for playback: %s\n", buffer_time, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_get_buffer_size(params, &size);
	if (err < 0) {
		printf("Unable to get buffer size for playback: %s\n", snd_strerror(err));
		return err;
	}
	buffer_size = size;
	/* set the period time */
	err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, &dir);
	if (err < 0) {
		printf("Unable to set period time %i for playback: %s\n", period_time, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_get_period_size(params, &size, &dir);
	if (err < 0) {
		printf("Unable to get period size for playback: %s\n", snd_strerror(err));
		return err;
	}
	period_size = size;
	/* write the parameters to device */
	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		printf("Unable to set hw params for playback: %s\n", snd_strerror(err));
		return err;
	}
	return 0;
}

static int set_swparams(snd_pcm_t *handle, snd_pcm_sw_params_t *swparams)
{
	int err;

	/* get the current swparams */
	err = snd_pcm_sw_params_current(handle, swparams);
	if (err < 0) {
		printf("Unable to determine current swparams for playback: %s\n", snd_strerror(err));
		return err;
	}
	/* start the transfer when the buffer is almost full: */
	/* (buffer_size / avail_min) * avail_min */
	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, (buffer_size / period_size) * period_size);
	if (err < 0) {
		printf("Unable to set start threshold mode for playback: %s\n", snd_strerror(err));
		return err;
	}
	/* allow the transfer when at least period_size samples can be processed */
	/* or disable this mechanism when period event is enabled (aka interrupt like style processing) */
	err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_event ? buffer_size : period_size);
	if (err < 0) {
		printf("Unable to set avail min for playback: %s\n", snd_strerror(err));
		return err;
	}
	/* enable period events when requested */
	if (period_event) {
		err = snd_pcm_sw_params_set_period_event(handle, swparams, 1);
		if (err < 0) {
			printf("Unable to set period event: %s\n", snd_strerror(err));
			return err;
		}
	}
	/* write the parameters to the playback device */
	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0) {
		printf("Unable to set sw params for playback: %s\n", snd_strerror(err));
		return err;
	}
	return 0;
}

static int xrun_recovery(snd_pcm_t *handle, int err)
{
	if (verbose)
		printf("stream recovery\n");
	if (err == -EPIPE) {	/* under-run */
		err = snd_pcm_prepare(handle);
		if (err < 0)
			printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
		return 0;
	} else if (err == -ESTRPIPE) {
		while ((err = snd_pcm_resume(handle)) == -EAGAIN)
			sleep(1);	/* wait until the suspend flag is released */
		if (err < 0) {
			err = snd_pcm_prepare(handle);
			if (err < 0)
				printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
		}
		return 0;
	}
	return err;
}

static void async_callback(snd_async_handler_t *ahandler)
{
	snd_pcm_t *handle = snd_async_handler_get_pcm(ahandler);
	struct async_private_data *data = snd_async_handler_get_callback_private(ahandler);
	signed short *samples = data->samples;
	snd_pcm_channel_area_t *areas = data->areas;
	snd_pcm_sframes_t avail;
	int err;
	
	avail = snd_pcm_avail_update(handle);
	while (avail >= period_size) {
		generate_sine(areas, 0, period_size, &data->phase);
		err = snd_pcm_writei(handle, samples, period_size);
		if (err < 0) {
			printf("Write error: %s\n", snd_strerror(err));
			exit(EXIT_FAILURE);
		}
		if (err != period_size) {
			printf("Write error: written %i expected %li\n", err, period_size);
			exit(EXIT_FAILURE);
		}
		avail = snd_pcm_avail_update(handle);
	}
}

/* This isn't a loop, it's just the old name for it in the example pcm.c
 * provided in alsa-lib. This returns and processing is carried out elsewhere.
 */
static int async_loop(snd_pcm_t *handle,
		      signed short *samples,
		      snd_pcm_channel_area_t *areas)
{
	int err, count;

	async_data.samples = samples;
	async_data.areas = areas;
	async_data.phase = 0;
	err = snd_async_add_pcm_handler(&async_ahandler, handle, async_callback, &async_data);
	if (err < 0) {
		printf("Unable to register async handler\n");
		return -1;
	}
	for (count = 0; count < 2; count++) {
		generate_sine(areas, 0, period_size, &async_data.phase);
		err = snd_pcm_writei(handle, samples, period_size);
		if (err < 0) {
			printf("Initial write error: %s\n", snd_strerror(err));
			return -1;
		}
		if (err != period_size) {
			printf("Initial write error: written %i expected %li\n", err, period_size);
			return -1;
		}
	}
	if (snd_pcm_state(handle) == SND_PCM_STATE_PREPARED) {
		err = snd_pcm_start(handle);
		if (err < 0) {
			printf("Start error: %s\n", snd_strerror(err));
			return -1;
		}
	}
    return 0;
}

struct transfer_method {
	const char *name;
	snd_pcm_access_t access;
	int (*transfer_loop)(snd_pcm_t *handle,
			     signed short *samples,
			     snd_pcm_channel_area_t *areas);
};

static struct transfer_method transfer_methods[] = {
	{ "async", SND_PCM_ACCESS_RW_INTERLEAVED, async_loop },
	{ NULL, SND_PCM_ACCESS_RW_INTERLEAVED, NULL }
};

audio_hw_err_t audio_hw_setup(audio_hw_setup_t *params)
{
    snd_pcm_t *handle;
    int err, morehelp;
    snd_pcm_hw_params_t *hwparams;
    snd_pcm_sw_params_t *swparams;
    int method = 0;
    signed short *samples;
    unsigned int chn;
    snd_pcm_channel_area_t *areas;

    snd_pcm_hw_params_alloca(&hwparams);
    snd_pcm_sw_params_alloca(&swparams);

    morehelp = 0;
    device = strdup(params->device);
    rate = params->rate < 4000 ? 4000 : params->rate;
    rate = params->rate > 196000 ? 196000 : params->rate;
    channels = params->channels < 1 ? 1 : params->channels;
    channels = params->channels > 1024 ? 1024 : params->channels;
    buffer_time = params->buffer_time < 1000 ? 1000 : params->buffer_time;
    buffer_time = params->buffer_time > 1000000 ? 1000000 : params->buffer_time;
    period_time = params->period_time < 1000 ? 1000 : params->period_time;
    period_time = params->period_time > 1000000 ? 1000000 : params->period_time;
    for (method = 0; transfer_methods[method].name; method++) {
        if (!strcasecmp(transfer_methods[method].name, params->method)) {
            break;
        }
    }
    if (transfer_methods[method].name == NULL) {
        method = 0;
    }
    format = params->format;
    if (format == SND_PCM_FORMAT_LAST) {
        format = SND_PCM_FORMAT_S16;
    }
    if (!snd_pcm_format_linear(format) &&
            !(format == SND_PCM_FORMAT_FLOAT_LE ||
                format == SND_PCM_FORMAT_FLOAT_BE)) {
        printf("Invalid (non-linear/float) format\n");
        return -1;
    }
    verbose = params->verbose;
    resample = params->resample;
    period_event = params->period_event;

	err = snd_output_stdio_attach(&output, stdout, 0);
	if (err < 0) {
		printf("Output failed: %s\n", snd_strerror(err));
		return -1;
	}

	printf("Playback device is %s\n", device);
	printf("Stream parameters are %iHz, %s, %i channels\n", rate, snd_pcm_format_name(format), channels);
	printf("Using transfer method: %s\n", transfer_methods[method].name);

	if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		return -1;
	}
	
	if ((err = set_hwparams(handle, hwparams, transfer_methods[method].access)) < 0) {
		printf("Setting of hwparams failed: %s\n", snd_strerror(err));
		return -1;
	}
	if ((err = set_swparams(handle, swparams)) < 0) {
		printf("Setting of swparams failed: %s\n", snd_strerror(err));
		return -1;
	}

	if (verbose > 0)
		snd_pcm_dump(handle, output);

    /* Allocates memory for ALSA */
	samples = malloc((period_size * channels * snd_pcm_format_physical_width(format)) / 8);
	if (samples == NULL) {
		printf("No enough memory\n");
		return -1;
	}
	areas = calloc(channels, sizeof(snd_pcm_channel_area_t));
	if (areas == NULL) {
		printf("No enough memory\n");
		return -1;
	}
	for (chn = 0; chn < channels; chn++) {
		areas[chn].addr = samples;
		areas[chn].first = chn * snd_pcm_format_physical_width(format);
		areas[chn].step = channels * snd_pcm_format_physical_width(format);
	}

    /* Allocate memory for audio_hw_io_struct */
    audiohwio.in = (audio_hw_sample_t*)malloc(
                        sizeof(audio_hw_sample_t)*period_size*channels);
    audiohwio.out = (audio_hw_sample_t*)malloc(
                        sizeof(audio_hw_sample_t)*period_size*channels);
    audiohwio.length = period_size; /* length in number of frames so actual size
                                       in samples is (audiohwio.length*channels) */
    audiohwio.nchans_in = channels;
    audiohwio.nchans_out = channels;

	err = transfer_methods[method].transfer_loop(handle, samples, areas);
	if (err < 0) {
		printf("Transfer failed: %s\n", snd_strerror(err));
        return -1;
    }

	return 0;
}
