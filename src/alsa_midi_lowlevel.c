#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "alsa/asoundlib.h"
#include <signal.h>
#include <unistd.h> 
#include "alsa_midi_lowlevel.h"

static int fd_in, fd_out;
static snd_rawmidi_t *handle_in, *handle_out;

midi_hw_err_t midi_hw_setup(midi_hw_setup_t *params)
{
	int i;
	int err;
	int thru=0;
	int verbose = params->verbose;
	char *device_in = params->device_in;
	char *device_out = NULL;
	char *node_in = NULL;
	char *node_out = NULL;
	
	fd_in = -1,fd_out = -1;
	handle_in = 0, handle_out = 0;
	
	if (verbose) {
		fprintf(stderr,"Using: \n");
		fprintf(stderr,"Input: ");
		if (device_in) {
			fprintf(stderr,"device %s\n",device_in);
		}else if (node_in){
			fprintf(stderr,"%s\n",node_in);	
		}else{
			fprintf(stderr,"NONE\n");
		}
		fprintf(stderr,"Output: ");
		if (device_out) {
			fprintf(stderr,"device %s\n",device_out);
		}else if (node_out){
			fprintf(stderr,"%s\n",node_out);
		}else{
			fprintf(stderr,"NONE\n");
		}
	}
	
	if (device_in) {
		err = snd_rawmidi_open(&handle_in,NULL,device_in,SND_RAWMIDI_NONBLOCK);	
		if (err) {
			fprintf(stderr,"snd_rawmidi_open %s failed: %d\n",device_in,err);
            return(-1);
		}
	}
	if (node_in && (!node_out || strcmp(node_out,node_in))) {
		fd_in = open(node_in,O_RDONLY);
		if (fd_in<0) {
			fprintf(stderr,"open %s for input failed\n",node_in);
            return(-1);
		}	
	}

	if (device_out) {
		err = snd_rawmidi_open(NULL,&handle_out,device_out,0);
		if (err) {
			fprintf(stderr,"snd_rawmidi_open %s failed: %d\n",device_out,err);
            return(-1);
		}
	}
	if (node_out && (!node_in || strcmp(node_out,node_in))) {
		fd_out = open(node_out,O_WRONLY);		
		if (fd_out<0) {
			fprintf(stderr,"open %s for output failed\n",node_out);
            return(-1);
		}	
	}

	if (node_in && node_out && strcmp(node_out,node_in)==0) {
		fd_in = fd_out = open(node_out,O_RDWR);		
		if (fd_out<0) {
			fprintf(stderr,"open %s for input and output failed\n",node_out);
            return(-1);
		}		
	}
    return(0);
}

midi_hw_err_t midi_hw_cleanup(midi_hw_cleanup_t *params)
{
	if (params->verbose) {
		fprintf(stderr,"Closing\n");
	}

	if (handle_in) {
		snd_rawmidi_drain(handle_in); 
		snd_rawmidi_close(handle_in);	
	}
	if (handle_out) {
		snd_rawmidi_drain(handle_out); 
		snd_rawmidi_close(handle_out);	
	}
	if (fd_in!=-1) {
		close(fd_in);
	}
	if (fd_out!=-1) {
		close(fd_out);
	}
	return 0;
}

void midi_hw_process_input(midi_hw_process_t *params)
{
    unsigned char ch;
    while (snd_rawmidi_read(handle_in,&ch,1) == 1) {
        midi_hw_process_byte(ch);
    }
}
