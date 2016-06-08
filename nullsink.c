
#include "module.h"

struct pa_null_sink {
	char *name;
	uint32_t module_index;
	uint32_t sink_index;
};


pa_null_sink *create_null_sink (struct userdata *u, const char *name)
{
	pa_core *core = u->core;
	pa_module *module;
	pa_sink *sink;
	pa_null_sink *ns;
	char args[256];
	int idx;

	 /* concatenate the arguments */
	snprintf (args, sizeof(args), "sink_name=\"%s\" channels=2", name);

	 /* load "module-null-sink" with these arguments */
	module = pa_module_load (core, "module-null-sink", args);

	if (module) {
		pa_log ("Successfully loaded 'module-null-sink'");
	} else {
		pa_log ("Error when loading 'module-null-sink'");
		return NULL;
	}

	/* now that the module is loaded, it should have created a NULL sink.
	 * iterate on all PulseAudio sinks, find one whose module pointer
	 * address is strictly the same as the module we just loaded ;
	 * it is our sink. Memorize its global numeric index */

	sink = NULL;
	PA_IDXSET_FOREACH(sink, core->sinks, idx) {
		if (sink->module && sink->module == module) {
			pa_log ("Found NULL sink named '%s', index number is %d\n", sink->name, sink->index);
			break;
		}
	}
	if (!sink) {
		pa_log ("Error when trying to find NULL sink");
		return NULL;
	}

	ns = pa_xnew0 (pa_null_sink, 1);
	ns->name = pa_xstrdup (name);
	ns->module_index = module->index;
	ns->sink_index = sink->index;

	return ns;
}

void destroy_null_sink (struct userdata *u, pa_null_sink *ns)
{
	pa_core *core = u->core;
	pa_module *module;

	module = pa_idxset_get_by_index (core->modules, ns->module_index);

	pa_module_unload (core, module, false);
	pa_log ("Successfully unloaded 'module-null-sink'");

	pa_xfree (ns->name);
	pa_xfree (ns);
}

uint32_t get_index_null_sink (pa_null_sink *ns)
{
	return ns->sink_index;
}
