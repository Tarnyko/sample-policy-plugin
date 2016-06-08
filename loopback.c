
#include "module.h"

struct pa_loopback {
	uint32_t module_index;
	uint32_t source_output_index;
	uint32_t sink_input_index;
};


pa_loopback *create_loopback (struct userdata *u, uint32_t source_index, uint32_t sink_index)
{
	pa_core *core = u->core;
	pa_module *module;
	pa_source *source;
	pa_sink *sink;
	pa_source_output *source_output;
	pa_sink_input *sink_input;
	pa_loopback *loopback;
	char args[512];
	int idx;

	source = pa_idxset_get_by_index (core->sources, source_index);
	if (!source) {
		pa_log ("Error when trying to find source index %d", source_index);
		return NULL;
	}
	pa_log ("Found source '%s' with index %d", source->name, source_index);

	sink = pa_idxset_get_by_index (core->sinks, sink_index);
	if (!sink) {
		pa_log ("Error when trying to find sink index", sink_index);
		return NULL;
	}
	pa_log ("Found sink '%s' with index %d", sink->name, sink_index);

	 /* concatenate the arguments */
	snprintf (args, sizeof(args), "source=\"%s\" sink=\"%s\"",
		  		      source->name, sink->name);

	 /* load "module-loopback" with these arguments */
	module = pa_module_load (core, "module-loopback", args);

	if (module) {
		pa_log ("Successfully loaded 'module-loopback'");
	} else {
		pa_log ("Error when loading 'module-loopback'");
		return NULL;
	}

	/* now that the module is loaded, it should have created corresponding
 	 * PulseAudio source output and sink input, find those whose module
 	 * pointer address is strictly the same as the module we just loaded ;
 	 * it is ours. Memorize their global numeric index */

	sink_input = NULL;
	PA_IDXSET_FOREACH(sink_input, core->sink_inputs, idx) {
		if (sink_input->module && sink_input->module == module) {
			break;
		}
	}
	if (!sink_input) {
		pa_log ("Error when trying to find sink input");
		return NULL;
	}


	source_output = NULL;
	PA_IDXSET_FOREACH(source_output, core->source_outputs, idx) {
		if (source_output->module && source_output->module == module) {
			break;
		}
	}
	if (!source_output) {
		pa_log ("Error when trying to find source output");
		return NULL;
	}

	loopback = pa_xnew0 (pa_loopback, 1);
	loopback->module_index = module->index;
	loopback->source_output_index = source_output->index;
	loopback->sink_input_index = sink_input->index;

	return loopback;
}

void destroy_loopback (struct userdata *u, pa_loopback *loopback)
{
	pa_core *core = u->core;
	pa_module *module;

	module = pa_idxset_get_by_index (core->modules, loopback->module_index);

	pa_module_unload (core, module, false);
	pa_log ("Successfully unloaded 'module-loopback'");

	pa_xfree (loopback);
}

uint32_t get_index_source_output_loopback (pa_loopback *loopback)
{
	return loopback->source_output_index;
}

uint32_t get_index_sink_input_loopback (pa_loopback *loopback)
{
	return loopback->sink_input_index;
}
