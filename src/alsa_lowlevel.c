#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include "../include/asoundlib.h"
#include <sys/time.h>
#include <math.h>

static char *device = "plughw:0,0";			/* playback device */
static snd_pcm_format_t format = SND_PCM_FORMAT_S16;	/* sample format */
static unsigned int rate = 44100;			/* stream rate */
static unsigned int channels = 1;			/* count of channels */
static unsigned int buffer_time = 500000;		/* ring buffer length in us */
static unsigned int period_time = 100000;		/* period time in us */
static double freq = 440;				/* sinusoidal wave frequency in Hz */
static int verbose = 0;					/* verbose flag */
static int resample = 1;				/* enable alsa-lib resampling */
static int period_event = 0;				/* produce poll event after each period */

static snd_pcm_sframes_t buffer_size;
static snd_pcm_sframes_t period_size;
static snd_output_t *output = NULL;

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
	/* fill the channel areas */
	while (count-- > 0) {
		union {
			float f;
			int i;
		} fval;
		int res, i;
		if (is_float) {
			fval.f = sin(phase) * maxval;
			res = fval.i;
		} else
			res = sin(phase) * maxval;
		if (to_unsigned)
			res ^= 1U << (format_bits - 1);
		for (chn = 0; chn < channels; chn++) {
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
		phase += step;
		if (phase >= max_phase)
			phase -= max_phase;
	}
	*_phase = phase;
}

audio_hw_err_t audio_hw_setup(audio_hw_setup_t params)
{
	struct option long_option[] =
	{
		{"help", 0, NULL, 'h'},
		{"device", 1, NULL, 'D'},
		{"rate", 1, NULL, 'r'},
		{"channels", 1, NULL, 'c'},
		{"frequency", 1, NULL, 'f'},
		{"buffer", 1, NULL, 'b'},
		{"period", 1, NULL, 'p'},
		{"method", 1, NULL, 'm'},
		{"format", 1, NULL, 'o'},
		{"verbose", 1, NULL, 'v'},
		{"noresample", 1, NULL, 'n'},
		{"pevent", 1, NULL, 'e'},
		{NULL, 0, NULL, 0},
	};
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
    params->channels = params->channels < 1 ? 1 : params->channels;
    params->channels = params->channels > 1024 ? 1024 : params->channels;
    freq = params->freq < 50 ? 50 : params->freq;
    freq = params->freq > 5000 ? 5000 : params->freq;
    buffer_time = params->buffer_time < 1000 ? 1000 : params->buffer_time;
    buffer_time = params->buffer_time > 1000000 ? 1000000 : params->buffer_time;
    period_time = params->period_time < 1000 ? 1000 : params->period_time;
    period_time = params->period_time > 1000000 ? 1000000 : params->period_time;

			for (method = 0; transfer_methods[method].name; method++)
					if (!strcasecmp(transfer_methods[method].name, optarg))
					break;
			if (transfer_methods[method].name == NULL)
				method = 0;
			break;
		case 'o':
			for (format = 0; format < SND_PCM_FORMAT_LAST; format++) {
				const char *format_name = snd_pcm_format_name(format);
				if (format_name)
					if (!strcasecmp(format_name, optarg))
					break;
			}
			if (format == SND_PCM_FORMAT_LAST)
				format = SND_PCM_FORMAT_S16;
			if (!snd_pcm_format_linear(format) &&
			    !(format == SND_PCM_FORMAT_FLOAT_LE ||
			      format == SND_PCM_FORMAT_FLOAT_BE)) {
				printf("Invalid (non-linear/float) format %s\n",
				       optarg);
				return 1;
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case 'n':
			resample = 0;
			break;
		case 'e':
			period_event = 1;
			break;
		}
	}

	if (morehelp) {
		help();
		return 0;
	}

	err = snd_output_stdio_attach(&output, stdout, 0);
	if (err < 0) {
		printf("Output failed: %s\n", snd_strerror(err));
		return 0;
	}

	printf("Playback device is %s\n", device);
	printf("Stream parameters are %iHz, %s, %i channels\n", rate, snd_pcm_format_name(format), channels);
	printf("Sine wave rate is %.4fHz\n", freq);
	printf("Using transfer method: %s\n", transfer_methods[method].name);

	if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		return 0;
	}
	
	if ((err = set_hwparams(handle, hwparams, transfer_methods[method].access)) < 0) {
		printf("Setting of hwparams failed: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}
	if ((err = set_swparams(handle, swparams)) < 0) {
		printf("Setting of swparams failed: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}

	if (verbose > 0)
		snd_pcm_dump(handle, output);

	samples = malloc((period_size * channels * snd_pcm_format_physical_width(format)) / 8);
	if (samples == NULL) {
		printf("No enough memory\n");
		exit(EXIT_FAILURE);
	}
	
	areas = calloc(channels, sizeof(snd_pcm_channel_area_t));
	if (areas == NULL) {
		printf("No enough memory\n");
		exit(EXIT_FAILURE);
	}
	for (chn = 0; chn < channels; chn++) {
		areas[chn].addr = samples;
		areas[chn].first = chn * snd_pcm_format_physical_width(format);
		areas[chn].step = channels * snd_pcm_format_physical_width(format);
	}

	err = transfer_methods[method].transfer_loop(handle, samples, areas);
	if (err < 0)
		printf("Transfer failed: %s\n", snd_strerror(err));

	free(areas);
	free(samples);
	snd_pcm_close(handle);
	return 0;
}

