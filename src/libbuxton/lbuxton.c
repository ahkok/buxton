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
#include <errno.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <dirent.h>
#include <string.h>
#include <stdint.h>

#include "util.h"
#include "bt-daemon.h"
#include "log.h"
#include "hashmap.h"
#include "protocol.h"

static Hashmap *_databases = NULL;
static Hashmap *_backends = NULL;

/**
 * Runs on exit to ensure all resources are correctly disposed of
 */
void exit_handler(void);
static bool _exit_handler_registered = false;

bool buxton_client_open(BuxtonClient *client)
{
	int bx_socket, r;
	struct sockaddr_un remote;

	assert(client);

	if (!_exit_handler_registered) {
		_exit_handler_registered = true;
		atexit(exit_handler);
	}

	if ((bx_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		return false;
	}

	remote.sun_family = AF_UNIX;
	strncpy(remote.sun_path, BUXTON_SOCKET, sizeof(remote.sun_path));
	r = connect(bx_socket, (struct sockaddr *)&remote, sizeof(remote));
	client->fd = bx_socket;
	if ( r == -1) {
		close(client->fd);
		return false;
	}

	return true;
}

void buxton_client_close(BuxtonClient *client)
{
	assert(client);

	if (buxton_direct_permitted(client))
		buxton_direct_revoke(client);
	else
		close(client->fd);
	client->direct = 0;
	client->fd = -1;
}

static bool init_backend(BuxtonLayer *layer, BuxtonBackend **backend)
{
	void *handle, *cast;
	_cleanup_free_ char *path = NULL;
	const char *name;
	char *error;
	int r;
	bool rb;
	module_init_func i_func;
	module_destroy_func d_func;
	BuxtonBackend *backend_tmp;

	assert(layer);
	assert(backend);

	if (layer->backend == BACKEND_GDBM)
		name = "gdbm";
	else if (layer->backend == BACKEND_MEMORY)
		name = "memory";
	else
		return false;

	backend_tmp = hashmap_get(_backends, name);

	if (backend_tmp) {
		*backend = backend_tmp;
		return true;
	}

	backend_tmp = malloc0(sizeof(BuxtonBackend));
	if (!backend_tmp)
		return false;

	r = asprintf(&path, "%s/%s.so", MODULE_DIRECTORY, name);
	if (r == -1)
		return false;

	/* Load the module */
	handle = dlopen(path, RTLD_LAZY);

	if (!handle) {
		buxton_log("dlopen(): %s\n", dlerror());
		return false;
	}

	dlerror();
	cast = dlsym(handle, "buxton_module_init");
	if ((error = dlerror()) != NULL || !cast) {
		buxton_log("dlsym(): %s\n", error);
		dlclose(handle);
		return false;
	}
	memcpy(&i_func, &cast, sizeof(i_func));
	dlerror();

	cast = dlsym(handle, "buxton_module_destroy");
	if ((error = dlerror()) != NULL || !cast) {
		buxton_log("dlsym(): %s\n", error);
		dlclose(handle);
		return false;
	}
	memcpy(&d_func, &cast, sizeof(d_func));

	rb = i_func(backend_tmp);
	if (!rb) {
		buxton_log("buxton_module_init failed\n");
		dlclose(handle);
		return false;
	}

	if (!_backends) {
		_backends = hashmap_new(trivial_hash_func, trivial_compare_func);
		if (!_backends) {
			dlclose(handle);
			return false;
		}
	}

	r = hashmap_put(_backends, name, backend_tmp);
	if (r != 1) {
		dlclose(handle);
		return false;
	}

	backend_tmp->module = handle;
	backend_tmp->destroy = d_func;

	*backend = backend_tmp;

	return true;
}

static BuxtonBackend *backend_for_layer(BuxtonLayer *layer)
{
	BuxtonBackend *backend;

	assert(layer);

	if (!_databases)
		_databases = hashmap_new(string_hash_func, string_compare_func);
	if ((backend = (BuxtonBackend*)hashmap_get(_databases, layer->name.value)) == NULL) {
		/* attempt load of backend */
		if (!init_backend(layer, &backend)) {
			buxton_log("backend_for_layer(): failed to initialise backend for layer: %s\n", layer->name);
			free(backend);
			return NULL;
		}
		hashmap_put(_databases, layer->name.value, backend);
	}
	return (BuxtonBackend*)hashmap_get(_databases, layer->name.value);
}

bool buxton_client_get_value(BuxtonClient *client,
			      BuxtonString *key,
			      BuxtonData *data)
{

	assert(client);
	assert(key);

	/*
	 * Only for testing, delete after non direct client support
	 * enabled
	 */
	if (buxton_direct_permitted(client)) {
		/* Handle direct manipulation */
		BuxtonLayer *l;
		BuxtonConfig *config;
		BuxtonString layer = (BuxtonString){ NULL, 0 };
		Iterator i;
		BuxtonData d;
		int priority = 0;
		int r;

		config = buxton_get_config(client);

		HASHMAP_FOREACH(l, config->layers, i) {
			r = buxton_client_get_value_for_layer(client,
							      &l->name,
							      key,
							      &d);
			if (r) {
				free(d.label.value);
				if (d.type == STRING)
					free(d.store.d_string.value);
				if (priority <= l->priority) {
					priority = l->priority;
					layer.value = l->name.value;
					layer.length = l->name.length;
				}
			}
		}
		if (layer.value) {
			return buxton_client_get_value_for_layer(client,
								 &layer,
								 key,
								 data);
		}
		return false;
	}

	/* Normal interaction (wire-protocol) */
	return buxton_wire_get_value(client, NULL, key, data);
}

bool buxton_client_get_value_for_layer(BuxtonClient *client,
			      BuxtonString *layer_name,
			      BuxtonString *key,
			      BuxtonData *data)
{

	assert(client);
	assert(layer_name);
	assert(layer_name->value);
	assert(key);

	/* TODO: Implement */
	if (buxton_direct_permitted(client)) {
		/* Handle direct manipulation */
		BuxtonBackend *backend = NULL;
		BuxtonLayer *layer = NULL;
		BuxtonConfig *config;

		config = buxton_get_config(client);
		if ((layer = hashmap_get(config->layers, layer_name->value)) == NULL) {
			return false;
		}
		backend = backend_for_layer(layer);
		if (!backend) {
			/* Already logged */
			return false;
		}
		layer->uid = client->uid;
		return backend->get_value(layer, key, data);
	}

	/* Normal interaction (wire-protocol) */
	return buxton_wire_get_value(client, layer_name, key, data);
}

bool buxton_client_register_notification(BuxtonClient *client, BuxtonString *key)
{
	assert(client);
	assert(key);

	if (buxton_direct_permitted(client)) {
		/* Direct notifications not currently supported */
		return false;
	}
	return buxton_wire_register_notification(client, key);
}

bool buxton_client_set_value(BuxtonClient *client,
			      BuxtonString *layer_name,
			      BuxtonString *key,
			      BuxtonData *data)
{

	assert(client);
	assert(layer_name);
	assert(layer_name->value);
	assert(key);
	assert(key->value);
	assert(data);
	assert(data->label.value);

	if (buxton_direct_permitted(client)) {
		/* Handle direct manipulation */
		BuxtonBackend *backend;
		BuxtonLayer *layer;
		BuxtonConfig *config;

		config = buxton_get_config(client);
		if ((layer = hashmap_get(config->layers, layer_name->value)) == NULL) {
			return false;
		}
		backend = backend_for_layer(layer);
		if (!backend) {
			/* Already logged */
			return false;
		}
		layer->uid = client->uid;
		return backend->set_value(layer, key, data);
	}

	/* Normal interaction (wire-protocol) */
	return buxton_wire_set_value(client, layer_name, key, data);
}

bool buxton_client_set_label(BuxtonClient *client,
			      BuxtonString *layer_name,
			      BuxtonString *key,
			      BuxtonString *label)
{
	BuxtonBackend *backend;
	BuxtonData data;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	bool r;

	assert(client);
	assert(layer_name);
	assert(layer_name->value);
	assert(key);
	assert(key->value);
	assert(label);
	assert(label->value);

	if (!buxton_direct_permitted(client))
		return false;

	config = buxton_get_config(client);
	/* Handle direct manipulation */
	if ((layer = hashmap_get(config->layers, layer_name->value)) == NULL) {
		return false;
	}
	backend = backend_for_layer(layer);
	if (!backend) {
		/* Already logged */
		return false;
	}

	char *name = buxton_get_name(key);
	if (name) {
		r = buxton_client_get_value_for_layer(client, layer_name, key, &data);
		if (!r)
			return false;

		free(data.label.value);
	} else {
		/* we have a group, so initialize the data with a dummy value */
		data.type = STRING;
		data.store.d_string = buxton_string_pack("BUXTON_GROUP_VALUE");
	}

	data.label.length = label->length;
	data.label.value = label->value;

	return backend->set_value(layer, key, &data);
}

bool buxton_client_unset_value(BuxtonClient *client,
			       BuxtonString *layer_name,
			       BuxtonString *key)
{

	assert(client);
	assert(layer_name);
	assert(layer_name->value);
	assert(key);
	assert(key->value);


	if (buxton_direct_permitted(client)) {
		/* Handle direct manipulation */
		BuxtonBackend *backend;
		BuxtonLayer *layer;
		BuxtonConfig *config;

		config = buxton_get_config(client);
		if ((layer = hashmap_get(config->layers, layer_name->value)) == NULL) {
			return false;
		}
		backend = backend_for_layer(layer);
		if (!backend) {
			/* Already logged */
			return false;
		}
		layer->uid = client->uid;
		return backend->unset_value(layer, key, NULL);
	}

	/* Normal interaction (wire-protocol) */
	return buxton_wire_unset_value(client, layer_name, key);
}

static void destroy_backend(BuxtonBackend *backend)
{

	assert(backend);

	backend->set_value = NULL;
	backend->get_value = NULL;
	backend->unset_value = NULL;
	backend->destroy();
	dlclose(backend->module);
	free(backend);
	backend = NULL;
}

void exit_handler(void)
{
	/* TODO: Remove from library, add buxton_direct_close */
	Iterator iterator;
	BuxtonBackend *backend;

	HASHMAP_FOREACH(backend, _backends, iterator) {
		destroy_backend(backend);
	}
	hashmap_free(_backends);
	hashmap_free(_databases);
	/*hashmap_free(_layers);*/
}

BuxtonString *buxton_make_key(char *group, char *name)
{
	BuxtonString *key;
	int len;

	if (!group)
		return NULL;

	key = malloc0(sizeof(BuxtonString));
	if (!key)
		return NULL;

	if (!name) {
		key->value = strdup(group);
		if (!key->value) {
			free(key);
			return NULL;
		}
		key->length = (uint32_t)strlen(key->value) + 1;
		return key;
	}

	len = asprintf(&(key->value), "%s%c%s", group, 0, name);
	if (len < 0) {
		free(key);
		return NULL;
	}
	key->length = (uint32_t)len + 1;

	return key;
}

char *buxton_get_group(BuxtonString *key)
{
	if (!key || !(key->value) || !(*(key->value)))
		return NULL;

	return key->value;
}

char *buxton_get_name(BuxtonString *key)
{
	char *c;

	if (!key || !(key->value))
		return NULL;

	c = strchr(key->value, 0);
	if (!c)
		return NULL;
	if (c - (key->value + (key->length - 1)) >= 0)
		return NULL;
	c++;

	return c;
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
