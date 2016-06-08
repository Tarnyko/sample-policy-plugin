#ifndef moduleh
#define moduleh

#define _GNU_SOURCE
#include <stdio.h>		/* for asprintf() */
#include <stdbool.h>		/* for true/false */
#include <stdint.h>		/* for uint32_t */
#include <stdlib.h>		/* for malloc(), realloc(), free() */

 /* THIS IS PROVIDED BY "pulseaudio-module-devel" package */
#include <pulsecore/pulsecore-config.h>
#include <pulsecore/module.h>

typedef struct client client;
typedef struct userdata userdata;
typedef struct pa_null_sink pa_null_sink;
typedef struct pa_loopback pa_loopback;

struct client {
	pa_client *client;
	pa_null_sink *ns;
	pa_loopback *loopback;
	bool ramped_down;
	char *role;
	int idx;
};

struct userdata {
        pa_core *core;
        pa_hook_slot *new_client_slot;
        pa_hook_slot *new_source_slot;
        pa_hook_slot *new_source_output_slot;
        pa_hook_slot *new_sink_slot;
        pa_hook_slot *new_sink_input_slot;
        pa_hook_slot *unlink_client_slot;
	client **clients;
	int client_count;
};

pa_null_sink *create_null_sink (struct userdata *, const char *);
void destroy_null_sink (struct userdata *, pa_null_sink *);
uint32_t get_index_null_sink (pa_null_sink *);

pa_loopback *create_loopback (struct userdata *, uint32_t source_index, uint32_t sink_index);
void destroy_loopback (struct userdata *, pa_loopback *);
uint32_t get_index_source_output_loopback (pa_loopback *);
uint32_t get_index_sink_input_loopback (pa_loopback *);

#endif
