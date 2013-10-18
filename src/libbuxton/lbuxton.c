/*
 * This file is part of buxton.
 *
 * Copyright (C) 2013 Intel Corporation
 *
 * buxton is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

/**
 * \file lbuxton.c Buxton library implementation
 */
#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdint.h>
#include <iniparser.h>

#include "../shared/util.h"
#include "../include/bt-daemon.h"
#include "../include/bt-daemon-private.h"
#include "../shared/log.h"
#include "../shared/hashmap.h"

static Hashmap *_databases = NULL;
static Hashmap *_directPermitted = NULL;
static Hashmap *_layers = NULL;

/**
 * Initialize layers using the configuration file
 * @return a boolean value, indicating success of the operation
 */
bool buxton_init_layers(void);
/**
 * Parse a given layer using the buxton configuration file
 * @param ini the configuration dictionary
 * @param name the layer to query
 * @param out The new BuxtonLayer to store
 * @return a boolean value, indicating success of the operation
 */
bool parse_layer(dictionary *ini, char *name, BuxtonLayer *out);

/**
 * Runs on exit to ensure all resources are correctly disposed of
 */
void exit_handler(void);
static bool _exit_handler_registered = false;

bool buxton_client_open(BuxtonClient *client)
{
	int bx_socket, r;
	struct sockaddr_un remote;
	bool ret;

	assert(client);

	if (!_exit_handler_registered) {
		_exit_handler_registered = true;
		atexit(exit_handler);
	}

	if ((bx_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		ret = false;
		goto end;
	}

	remote.sun_family = AF_UNIX;
	strncpy(remote.sun_path, BUXTON_SOCKET, sizeof(remote.sun_path));
	r = connect(bx_socket, (struct sockaddr *)&remote, sizeof(remote));
	client->fd = bx_socket;
	if ( r == -1) {
		ret = false;
		goto close;
	}

	ret = true;
close:
	/* Will be moved to a buxton_client_close method */
	close(client->fd);
end:
	return ret;
}

bool buxton_direct_open(BuxtonClient *client)
{

	assert(client);

	if (!_exit_handler_registered) {
		_exit_handler_registered = true;
		atexit(exit_handler);
	}

	if (!_directPermitted)
		_directPermitted = hashmap_new(trivial_hash_func, trivial_compare_func);

	if (!_layers)
		buxton_init_layers();

	client->direct = true;
	client->pid = getpid();
	hashmap_put(_directPermitted, &(client->pid), client);
	return true;
}

bool buxton_client_get_value(BuxtonClient *client,
			      const char *layer_name,
			      const char *key,
			      BuxtonData *data)
{

	assert(client);
	assert(layer_name);
	assert(key);

	/* TODO: Implement */
	if (_directPermitted && client->direct &&  hashmap_get(_directPermitted, &(client->pid)) == client) {
		/* Handle direct manipulation */
		BuxtonBackend *backend;
		BuxtonLayer *layer;
		if ((layer = hashmap_get(_layers, layer_name)) == NULL) {
			return false;
		}
		backend = backend_for_layer(layer);
		if (!backend) {
			/* Already logged */
			return false;
		}
		layer->uid = geteuid();
		return backend->get_value(layer, key, data);
	}

	/* Normal interaction (wire-protocol) */
	return false;
}

bool buxton_client_set_value(BuxtonClient *client,
			      const char *layer_name,
			      const char *key,
			      BuxtonData *data)
{

	assert(client);
	assert(layer_name);
	assert(key);
	assert(data);

	/* TODO: Implement */
	if (_directPermitted && client->direct &&  hashmap_get(_directPermitted, &(client->pid)) == client) {
		/* Handle direct manipulation */
		BuxtonBackend *backend;
		BuxtonLayer *layer;
		if ((layer = hashmap_get(_layers, layer_name)) == NULL) {
			return false;
		}
		backend = backend_for_layer(layer);
		if (!backend) {
			/* Already logged */
			return false;
		}
		layer->uid = geteuid();
		return backend->set_value(layer, key, data);
	}

	/* Normal interaction (wire-protocol) */
	return false;
}

BuxtonBackend* backend_for_layer(BuxtonLayer *layer)
{
	BuxtonBackend *backend;

	assert(layer);

	if (!_databases)
		_databases = hashmap_new(string_hash_func, string_compare_func);
	if ((backend = (BuxtonBackend*)hashmap_get(_databases, layer->name)) == NULL) {
		/* attempt load of backend */
		backend = malloc0(sizeof(BuxtonBackend));
		if (!backend)
			return NULL;
		if (!init_backend(layer, backend)) {
			buxton_log("backend_for_layer(): failed to initialise backend for layer: %s\n", layer->name);
			free(backend);
			return NULL;
		}
		hashmap_put(_databases, layer->name, backend);
	}
	return (BuxtonBackend*)hashmap_get(_databases, layer->name);
}

void destroy_backend(BuxtonBackend *backend)
{

	assert(backend);

	backend->set_value = NULL;
	backend->get_value = NULL;
	backend->destroy();
	dlclose(backend->module);
	free(backend);
	backend = NULL;
}

bool init_backend(BuxtonLayer *layer, BuxtonBackend* backend)
{
	void *handle, *cast;
	char *path;
	const char *name;
	char *error;
	int r;
	module_init_func i_func;
	module_destroy_func d_func;

	assert(layer);
	assert(backend);

	if (layer->backend == BACKEND_GDBM)
		name = "gdbm";
	else if (layer->backend == BACKEND_MEMORY)
		name = "memory";
	else
		return false;

	r = asprintf(&path, "%s/%s.so", MODULE_DIRECTORY, name);
	if (r == -1)
		return false;

	/* Load the module */
	handle = dlopen(path, RTLD_LAZY);
	free(path);

	if (!handle) {
		buxton_log("dlopen(): %s\n", dlerror());
		return false;
	}

	dlerror();
	cast = dlsym(handle, "buxton_module_init");
	if ((error = dlerror()) != NULL || !cast) {
		buxton_log("dlsym(): %s", error);
		dlclose(handle);
		return false;
	}
	memcpy(&i_func, &cast, sizeof(i_func));
	dlerror();

	cast = dlsym(handle, "buxton_module_destroy");
	if ((error = dlerror()) != NULL || !cast) {
		buxton_log("dlsym(): %s", error);
		dlclose(handle);
		return false;
	}
	memcpy(&d_func, &cast, sizeof(d_func));

	i_func(backend);
	if (backend == NULL) {
		buxton_log("buxton_module_init returned NULL");
		dlclose(handle);
		return false;
	}
	backend->module = handle;
	backend->destroy = d_func;

	return true;
}

/* Load layer configurations from disk */
bool buxton_init_layers(void)
{
	bool ret = false;
	dictionary *ini;
	const char *path = DEFAULT_CONFIGURATION_FILE;
	int nlayers = 0;

	ini = iniparser_load(path);
	if (ini == NULL) {
		buxton_log("Failed to load buxton conf file: %s\n", path);
		goto finish;
	}

	nlayers = iniparser_getnsec(ini);
	if (nlayers <= 0) {
		buxton_log("No layers defined in buxton conf file: %s\n", path);
		goto end;
	}

	_layers = hashmap_new(string_hash_func, string_compare_func);
	if (!_layers)
		goto end;

	for (int n = 0; n < nlayers; n++) {
		BuxtonLayer *layer;
		char *section_name;

		layer = malloc0(sizeof(BuxtonLayer));
		if (!layer)
			continue;

		section_name = iniparser_getsecname(ini, n);
		if (!section_name) {
			buxton_log("Failed to find section number: %d\n", n);
			continue;
		}

		if (!parse_layer(ini, section_name, layer)) {
			free(layer);
			buxton_log("Failed to load layer: %s\n", section_name);
			continue;
		}
		hashmap_put(_layers, layer->name, layer);
	}
	ret = true;

end:
	iniparser_freedict(ini);
finish:
	return ret;
}

bool parse_layer(dictionary *ini, char *name, BuxtonLayer *out)
{
	bool ret = false;
	int r;
	char *k_desc = NULL;
	char *k_backend = NULL;
	char *k_type = NULL;
	char *k_priority = NULL;
	char *_desc = NULL;
	char *_backend = NULL;
	char *_type = NULL;
	char *_priority = NULL;

	assert(ini);
	assert(name);
	assert(out);

	r = asprintf(&k_desc, "%s:description", name);
	if (r == -1)
		goto end;

	r = asprintf(&k_backend, "%s:backend", name);
	if (r == -1)
		goto end;

	r = asprintf(&k_type, "%s:type", name);
	if (r == -1)
		goto end;

	r = asprintf(&k_priority, "%s:priority", name);
	if (r == -1)
		goto end;

	_type = iniparser_getstring(ini, k_type, NULL);
	_backend = iniparser_getstring(ini, k_backend, NULL);
	_priority = iniparser_getstring(ini, k_priority, NULL);
	_desc = iniparser_getstring(ini, k_desc, NULL);

	if (!_type || !name || !_backend || !_priority)
		goto end;

	out->name = strdup(name);
	if (!out->name)
		goto fail;

	if (strcmp(_type, "System") == 0)
		out->type = LAYER_SYSTEM;
	else if (strcmp(_type, "User") == 0)
		out->type = LAYER_USER;
	else {
		buxton_log("Layer %s has unknown type: %s\n", name, _type);
		goto fail;
	}

	if (strcmp(_backend, "gdbm") == 0)
		out->backend = BACKEND_GDBM;
	else if(strcmp(_backend, "memory") == 0)
		out->backend = BACKEND_MEMORY;
	else
		goto fail;

	out->priority = strdup(_backend);
	if (!out->priority)
		goto fail;

	if (_desc != NULL)
		out->description = strdup(_desc);

	ret = true;
	goto end;

fail:
	if (out->name)
		free(out->name);
	if (out->description)
		free(out->description);
	if (out->priority)
		free(out->priority);

end:
	if (k_desc)
		free(k_desc);
	if (k_backend)
		free(k_backend);
	if (k_type)
		free(k_type);
	if (k_priority)
		free(k_priority);

	return ret;
}

void exit_handler(void)
{
	const char *key;
	Iterator iterator;
	BuxtonBackend *backend;

	HASHMAP_FOREACH_KEY(backend, key, _databases, iterator) {
		destroy_backend(backend);
	}
	hashmap_free(_databases);
	hashmap_free(_layers);
}

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
