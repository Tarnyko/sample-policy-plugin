
/* Load the module with :
$ pulseaudio --system --dl-search-path=/mypath
$ pactl load-module mypamodule
 (PS : /mypath must be world-readable, because PulseAudio drops
  its own privileges after initial startup)
*/

#include "module.h"

static pa_hook_result_t new_client_cb (pa_core *, pa_client_new_data *, void *);
static pa_hook_result_t new_source_cb (pa_core *, pa_source_new_data *, void *);
static pa_hook_result_t new_source_output_cb (pa_core *, pa_source_output_new_data *, void *);
static pa_hook_result_t new_sink_cb (pa_core *, pa_sink_new_data *, void *);
static pa_hook_result_t new_sink_input_cb (pa_core *, pa_sink_input_new_data *, void *);

static pa_hook_result_t unlink_client_cb (pa_core *, pa_client *, void *);


 /* LOCAL HELPER FUNCTIONS */

void destroy_client (struct userdata *u, struct client *client)
{
	if (client->ns)
		destroy_null_sink (u, client->ns);
	if (client->loopback)
		destroy_loopback (u, client->loopback);
	if (client->role)
		free (client->role);
	client->role = NULL;
	client->client = NULL;
	client->idx = -1;
}

pa_sink* find_primary_alsa_sink (pa_core *core)
{
	pa_sink *sink;
	int idx;

	PA_IDXSET_FOREACH(sink, core->sinks, idx) {
		if (sink->name && strstr (sink->name, "alsa")) {
			pa_log ("ALSA SINK NAME : %s", sink->name);
			return sink;
		}
	}

	return NULL;
}

 /* ----------------------- */


 /* INITIALIZATION FUNCTION */

int pa__init (pa_module *m)
{
	struct userdata *u;

	u = pa_xnew (struct userdata, 1);
	u->core = m->core;
	u->clients = malloc (sizeof(client*));
	u->client_count = 0;
	u->new_client_slot = pa_hook_connect (&m->core->hooks[PA_CORE_HOOK_CLIENT_NEW],
					      PA_HOOK_LATE,
					      (pa_hook_cb_t)new_client_cb,
					      u);
	u->unlink_client_slot = pa_hook_connect (&m->core->hooks[PA_CORE_HOOK_CLIENT_UNLINK],
						 PA_HOOK_LATE,
						 (pa_hook_cb_t)unlink_client_cb,
						 u);
	u->new_source_slot = pa_hook_connect (&m->core->hooks[PA_CORE_HOOK_SOURCE_NEW],
					      PA_HOOK_LATE,
					      (pa_hook_cb_t)new_source_cb,
					      u);
	u->new_source_output_slot = pa_hook_connect (&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_NEW],
					             PA_HOOK_LATE,
					             (pa_hook_cb_t)new_source_output_cb,
					              u);
	u->new_sink_slot = pa_hook_connect (&m->core->hooks[PA_CORE_HOOK_SINK_NEW],
					    PA_HOOK_LATE,
					    (pa_hook_cb_t)new_sink_cb,
					    u);
	u->new_sink_slot = pa_hook_connect (&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW],
					    PA_HOOK_LATE,
					    (pa_hook_cb_t)new_sink_input_cb,
					    u);

	m->userdata = u;

	pa_log ("Initializing \"mypamodule-newclient\"");

	return 0;
}


 /* CLOSEUP FUNCTION */
void pa__done (pa_module *m)
{
	struct userdata *u = m->userdata;
	struct client *client;
	int idx;

	for (idx = 0; idx < u->client_count; idx++) {
		client = u->clients[idx];
		destroy_client (u, client);
	}
	free (u->clients);
	pa_xfree (m->userdata);

	pa_log ("Closing \"mypamodule-newclient\"");
}

 /* ----------------------- */


 /* CALLBACKS */
static pa_hook_result_t new_client_cb (pa_core *core, pa_client_new_data *new_data, void *userdata)
{
	pa_proplist *proplist = new_data->proplist;
	const char *name;

	/* so early, only this property is available, and it is
         * totally generic (defined by PulseAudio)
         */
	name = pa_proplist_gets (proplist, PA_PROP_APPLICATION_NAME);

	if (name) {
		pa_log ("Name : %s", name);
		if (strstr (name , "UNIX socket client"))
			pa_log ("Client connection via socket !");
		else if (strstr (name, "TCP/IP client from"))
			pa_log ("Client connection via TCP/IP");
	} else {
		pa_log ("Name : not found");
	}

	return PA_HOOK_OK;
}

static pa_hook_result_t unlink_client_cb (pa_core *core, pa_client *client, void *userdata)
{
	struct userdata *u = userdata;
	struct client *thisclient, *client_t;
	bool is_phone, other_phones;
	int idx, jdx;

	pa_log ("Disconnecting client detected");

	if (u->client_count == 0)
		return PA_HOOK_OK;

	/* if client had "phone" role, it means that we force-muted all roled
	 * non-phone clients. Then iterate on all other clients, verify there
	 * is no other "phone" client left, and re-enable them */

	/* was it a phone ? */

	is_phone = false;
	for (idx = 0; idx < u->client_count; idx++) {
		thisclient = u->clients[idx];
		if (thisclient->client == client) {
			/* this is our client - is it a phone ? */
			if (thisclient->role && (strcmp (thisclient->role, "phone") == 0 )) {
				is_phone = true;
			}
			/* destroy client in all cases */
			destroy_client (u, thisclient);
			u->client_count--;
			break;
		}
	}

	if (!is_phone)
		return PA_HOOK_OK;

	/* were there other phones ? */

	other_phones = false;
	for (idx = 0; idx < u->client_count; idx++) {
		client_t = u->clients[idx];
		if (client_t->role && (strcmp (client_t->role, "phone") == 0)) {
			other_phones = true;
			break;
		}
	}

	/* if there were no other phones, re-enable all playback */

	/* find primary ALSA sink */
	pa_sink *alsa_sink = find_primary_alsa_sink (core);

	pa_source *source = NULL;
	if (!other_phones) {
		for (idx = 0; idx < u->client_count; idx++) {
			client_t = u->clients[idx];
			/* find NULL sink where the client is attached to */
			pa_sink *ns_sink = pa_idxset_get_by_index (core->sinks,
								   get_index_null_sink (client_t->ns));
			if (!ns_sink)
				continue;
			/* find source matching this NULL sink */
			PA_IDXSET_FOREACH(source, core->sources, jdx) {
                		if (source->name && strstr (source->name, ns_sink->name))
					break;
				source = NULL;
			}
			if (!source)
				continue;
			/* we can now either... */
			if (client_t->ramped_down) {
				/* ...ramp up "navi" clients */
				pa_log ("Restoring volume for app");
				client_t->ramped_down = false;
				pa_sink_input *sink_input;
				PA_IDXSET_FOREACH(sink_input, core->sink_inputs, jdx) {
					if (sink_input->sink && sink_input->sink == ns_sink)
						break;
				}
				/* ramp down this sink input's volume */
				pa_cvolume_ramp rampvol;
				long time = 5000;			/* 5 seconds */
				pa_volume_t newvol = PA_VOLUME_NORM;	/* max volume */
				pa_cvolume_ramp_set (&rampvol,
						     sink_input->volume.channels,
						     PA_VOLUME_RAMP_TYPE_LINEAR,
						     time, newvol);
				pa_sink_input_set_volume_ramp (sink_input, &rampvol, true, false);
			} else {
				/* ... restore sound/recreate loopback to ALSA for other "roled" clients */
				pa_log ("Restoring sound for app");
				client_t->loopback = create_loopback (u, source->index, alsa_sink->index);
			}
		}
	}

	return PA_HOOK_OK;
}

static pa_hook_result_t new_source_cb (pa_core *core, pa_source_new_data *new_data, void *userdata)
{
	pa_log ("New source detected");

	pa_log ("Source name : %s", new_data->name);

	return PA_HOOK_OK;
}

static pa_hook_result_t new_source_output_cb (pa_core *core, pa_source_output_new_data *new_data, void *userdata)
{
	pa_log ("New source output detected");

	return PA_HOOK_OK;
}

static pa_hook_result_t new_sink_cb (pa_core *core, pa_sink_new_data *new_data, void *userdata)
{
	pa_log ("New sink detected");

	return PA_HOOK_OK;
}

static pa_hook_result_t new_sink_input_cb (pa_core *core, pa_sink_input_new_data *new_data, void *userdata)
{
	struct userdata *u = userdata;
	struct client *client;
	bool new_client;
	pa_proplist *props = new_data->proplist;
	const char *prop;
	void *state = NULL; /* being NULL at the beginning is REQUIRED ! */
	const char *role = NULL;
	int idx, jdx;

	pa_log ("New sink input detected");

	/* is this a new client ? */

	new_client = true;

	if (u->client_count > 0) {
		for (idx = 0; idx < u->client_count; idx++) {
			client = u->clients[idx];
			if (client->client == new_data->client) {
				new_client = false;
				break;
			}
		}
	}

	/* if it is, register is in the global array */

	if (new_client) {
		u->clients = realloc (u->clients, (u->client_count+1)*sizeof(struct client*));
		u->clients[u->client_count] = malloc (sizeof(struct client));
 		client = u->clients[u->client_count];
		client->client = new_data->client; /* points to "pa_client" */
		client->idx = u->client_count;	   /* index, 0-n */
		client->ramped_down = false;
		client->role = NULL;
		u->client_count++;
	}

	/* obtain the client "media.role" property */

	prop = pa_proplist_iterate (props, &state);
	while (prop) {
		const char *value = pa_proplist_gets (props, prop);
		pa_log ("PROP = %s - VALUE = %s", prop, value);

		/* if PULSE_PROP="media.role=" has been set, memorize it */
		if (strcmp (prop, "media.role") == 0)
			role = value;

		prop = pa_proplist_iterate (props, &state);
	}
	 /* if PULSE_PROP="media.role=" has not been set, ignore and return */
	if (!role)
		return PA_HOOK_OK;

	/* NULL SINK PART */
	/* if this is a new client, we create a NULL sink and force-plug the
	 *  stream to it. Otherwise, it was already done, we skip */

	if (new_client) {
		char *ns_name;
		asprintf (&ns_name, "null.agl.%d", client->idx);
		client->ns = create_null_sink (u, ns_name);
		free (ns_name);

		if (!client->ns) {
			pa_log ("Error when creating new NULL sink, returning");
			return PA_HOOK_OK;
		}
	}

	pa_sink *ns_sink = pa_idxset_get_by_index (core->sinks,
	        	                           get_index_null_sink (client->ns));
	/* Existing client but no sink ? Avoiding this case */
	if (!ns_sink)
		return PA_HOOK_OK;

	if (new_client) {
		pa_log ("Attaching this sink input to NULL sink now...");
		pa_sink_input_new_data_set_sink (new_data, ns_sink, false);
	}

	/* We should now have at least 2 sinks (destinations) :
	 * the default ALSA one (to use, our adapter), and one NULL one.
	 * Let us find the first valid ALSA one. */
	pa_sink *alsa_sink = find_primary_alsa_sink (core);

	/* We should now have between 2 and n sources :
	 * the default ALSA one (unused), and our NULL ones (one per
	 * client, where they are playing from).
	 * Let us find the good NULL one by its name, which is the
	 * same than the NULL sink and ends with "monitor". */
	pa_source *source;
	PA_IDXSET_FOREACH(source, core->sources, idx) {
		if (source->name && strstr (source->name, ns_sink->name)) {
			pa_log ("NULL SOURCE NAME : %s", source->name);
			break;
		}
	}

	/* Now that we use have our NULL source and the ALSA sink (destination)
	 * plug them together with "module-loopback" */

	if (new_client) {
		client->loopback = create_loopback (u, source->index, alsa_sink->index);
		if (!client->loopback) {
			pa_log ("Error when creating loopback, returning");
			return PA_HOOK_OK;
		}
	}

	/* ROLES PART */
	/* here we could test for "new_client == true", but there is a race :
	 * loading "module_loopback" just before will fire a new "sink_input"
	 * callback with same client, thus "new_client" will be false */
	if (!client->role)
		client->role = strdup (role);

	/* If we are a phone, let us mute all non-phone "roled" applications,
 	 * NEW! (except the "navi" ones, ramp them down to 10%)
 	 * expect the "abstract" ones (which is an internal PulseAudio role) */
	struct client *client_i;
	if (strcmp (client->role, "phone") == 0) {
		for (idx = 0; idx < u->client_count; idx++) {
			client_i = u->clients[idx];
			if (client_i->role &&
			    ((strcmp (client_i->role, "phone") != 0) &&
			     (strcmp (client_i->role, "abstract") != 0))) {
				/* this is a "navi" app, just reduce the volume */
				if (strcmp (client_i->role, "navi") == 0) {
					pa_log ("Reducing volume for app");
					client_i->ramped_down = true;
					/* find sink input associated with client's fixed NULL sink */
					pa_sink *ns_sink_i = pa_idxset_get_by_index (core->sinks,
										     get_index_null_sink (client_i->ns));
					pa_sink_input *sink_input;
					PA_IDXSET_FOREACH(sink_input, core->sink_inputs, jdx) {
						if (sink_input->sink && sink_input->sink == ns_sink_i)
							break;
					}
					/* ramp down this sink input's volume */
					pa_cvolume_ramp rampvol;
					long time = 3000;				/* 3 seconds */
					pa_volume_t newvol = PA_VOLUME_NORM *10/100;	/* 10% of max volume */
					pa_cvolume_ramp_set (&rampvol,
							     sink_input->volume.channels,
							     PA_VOLUME_RAMP_TYPE_LINEAR,
							     time, newvol);
					pa_sink_input_set_volume_ramp (sink_input, &rampvol, true, false);
				/* this is a "non-navi" app, mute it ! */
				} else {
					destroy_loopback (u, client_i->loopback);
				}
			}
		}
	}

	return PA_HOOK_OK;
}
