#ifndef ALSA_MIDI_LOWLEVEL_H
#define ALSA_MIDI_LOWLEVEL_H 

typedef int midi_hw_err_t;

typedef int midi_hw_process_t;

typedef struct {
    char *device_in;
    int verbose;
} midi_hw_setup_t;

typedef midi_hw_setup_t midi_hw_cleanup_t;

#endif /* ALSA_MIDI_LOWLEVEL_H */
