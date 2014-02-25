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

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "direct.h"
#include "log.h"
#include "smack.h"
#include "util.h"

#define BUXTON_ROOT_CHECK_ENV "BUXTON_ROOT_CHECK"

bool buxton_direct_open(BuxtonControl *control)
{

	assert(control);

	memzero(&(control->config), sizeof(BuxtonConfig));
	if(!buxton_init_layers(&(control->config)))
		return false;

	control->client.direct = true;
	control->client.pid = getpid();

	return true;
}

int32_t buxton_direct_get_value(BuxtonControl *control, _BuxtonKey *key,
			     BuxtonData *data, BuxtonString *data_label,
			     BuxtonString *client_label)
{
	/* Handle direct manipulation */
	BuxtonLayer *l;
	BuxtonConfig *config;
	BuxtonString layer = (BuxtonString){ NULL, 0 };
	Iterator i;
	BuxtonData d;
	int priority = 0;
	int32_t r;
	BuxtonLayerType layer_origin = -1;

	assert(control);
	assert(key);

	if (key->layer.value)
		return buxton_direct_get_value_for_layer(control, key, data,
							 data_label,
							 client_label);

	config = &control->config;

	HASHMAP_FOREACH(l, config->layers, i) {
		key->layer.value = l->name.value;
		key->layer.length = l->name.length;
		r = buxton_direct_get_value_for_layer(control,
						      key,
						      &d,
						      data_label,
						      client_label);
		if (r) {
			free(data_label->value);
			data_label->value = NULL;
			data_label->length = 0;
			if (d.type == STRING)
				free(d.store.d_string.value);

			if ((l->type == LAYER_SYSTEM && (layer_origin != LAYER_SYSTEM ||
							 priority <= l->priority)) ||
			    (l->type == LAYER_USER && layer_origin != LAYER_SYSTEM &&
			     priority <= l->priority)) {
				if (l->type == LAYER_SYSTEM)
					layer_origin = LAYER_SYSTEM;
				else
					layer_origin = LAYER_USER;
				priority = l->priority;
				layer.value = l->name.value;
				layer.length = l->name.length;
			}
		}
	}
	if (layer.value) {
		key->layer.value = layer.value;
		key->layer.length = layer.length;
		r = buxton_direct_get_value_for_layer(control,
						      key,
						      data,
						      data_label,
						      client_label);
		key->layer.value = NULL;
		key->layer.length = 0;
		return r;
	}
	return ENOENT;
}

int32_t buxton_direct_get_value_for_layer(BuxtonControl *control,
				       _BuxtonKey *key,
				       BuxtonData *data,
				       BuxtonString *data_label,
				       BuxtonString *client_label)
{
	/* Handle direct manipulation */
	BuxtonBackend *backend = NULL;
	BuxtonLayer *layer = NULL;
	BuxtonConfig *config;
	BuxtonData g;
	_BuxtonKey group;
	BuxtonString group_label;
	int32_t ret = BUXTON_STATUS_FAILED;

	assert(control);
	assert(key);
	assert(data_label);

	buxton_debug("get_value for layer start\n");

	if (!key->layer.value)
		goto fail;

	config = &control->config;
	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL)
		goto fail;

	backend = backend_for_layer(config, layer);
	if (!backend) {
		/* Already logged */
		goto fail;
	}
	layer->uid = control->client.uid;

	/* Groups must be created first, so bail if this key's group doesn't exist */
	if (key->name.value) {
		if (!buxton_copy_key_group(key, &group))
			goto fail;

		ret = buxton_direct_get_value_for_layer(control, &group, &g, &group_label, NULL);
		if (ret) {
			buxton_debug("Group %s for name %s missing for get value\n",
						key->group.value, key->name.value);
			goto fail;
		}
	}

	/* The group checks are only needed for key lookups, or we recurse endlessly */
	if (key->name.value && client_label)
		if (!buxton_check_smack_access(client_label, &group_label, ACCESS_READ)) {
			ret = EPERM;
			goto fail;
		}

	if (backend->get_value(layer, key, data, data_label)) {
		/* Access checks are not needed for direct clients, where client_label is NULL */
		if (data_label->value && client_label && client_label->value &&
		    !buxton_check_smack_access(client_label, data_label, ACCESS_READ)) {
			/* Client lacks permission to read the value */
			free(data_label->value);
			ret = EPERM;
			goto fail;
		}
		buxton_debug("SMACK check succeeded for get_value for layer %s\n", key->layer.value);
		ret = BUXTON_STATUS_OK;
	}

fail:
	buxton_debug("get_value for layer end\n");
	return ret;
}

int32_t buxton_direct_set_value(BuxtonControl *control,
			     _BuxtonKey *key,
			     BuxtonData *data,
			     BuxtonString *label)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	BuxtonString default_label = buxton_string_pack("_");
	BuxtonString *l;
	_cleanup_buxton_data_ BuxtonData *d = NULL;
	_cleanup_buxton_data_ BuxtonData *g = NULL;
	_cleanup_buxton_key_ _BuxtonKey *group = NULL;
	_cleanup_buxton_string_ BuxtonString *data_label = NULL;
	_cleanup_buxton_string_ BuxtonString *group_label = NULL;
	int32_t ret = BUXTON_STATUS_FAILED;

	assert(control);
	assert(key);
	assert(data);

	buxton_debug("set_value start\n");

	group = malloc0(sizeof(_BuxtonKey));
	if (!group)
		goto fail;
	g = malloc0(sizeof(BuxtonData));
	if (!g)
		goto fail;
	group_label = malloc0(sizeof(BuxtonString));
	if (!group_label)
		goto fail;

	d = malloc0(sizeof(BuxtonData));
	if (!d)
		goto fail;
	data_label = malloc0(sizeof(BuxtonString));
	if (!data_label)
		goto fail;

	/* Groups must be created first, so bail if this key's group doesn't exist */
	if (!buxton_copy_key_group(key, group))
		goto fail;

	if (!buxton_direct_get_value_for_layer(control, group, g, group_label, NULL)) {
		buxton_debug("Group %s for name %s missing for set value\n", key->group.value, key->name.value);
		goto fail;
	}

	/* Access checks are not needed for direct clients, where label is NULL */
	if (label) {
		if (!buxton_check_smack_access(label, group_label, ACCESS_WRITE))
			goto fail;
		if (buxton_direct_get_value_for_layer(control, key, d, data_label, NULL)) {
			if (!buxton_check_smack_access(label, data_label, ACCESS_WRITE)) {
				goto fail;
			}
			l = data_label;
		} else {
			l = label;
		}
	} else {
		if (buxton_direct_get_value_for_layer(control, key, d, data_label, NULL)) {
			l = data_label;
		} else {
			l = &default_label;
		}
	}

	config = &control->config;
	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL)
		goto fail;

	backend = backend_for_layer(config, layer);
	if (!backend) {
		/* Already logged */
		goto fail;
	}

	layer->uid = control->client.uid;
	ret = backend->set_value(layer, key, data, l);

fail:
	buxton_debug("set_value end\n");
	return ret;
}

int32_t buxton_direct_set_label(BuxtonControl *control,
			     _BuxtonKey *key,
			     BuxtonString *label)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	_cleanup_buxton_data_ BuxtonData *data = NULL;
	_cleanup_buxton_string_ BuxtonString *data_label = NULL;
	int32_t ret = BUXTON_STATUS_OK;

	assert(control);
	assert(key);
	assert(label);

	data = malloc0(sizeof(BuxtonData));
	if (!data)
		goto fail;
	data_label = malloc0(sizeof(BuxtonString));
	if (!data_label)
		goto fail;

	config = &control->config;

	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
		goto fail;
	}

	if (layer->type == LAYER_SYSTEM) {
		char *root_check = getenv(BUXTON_ROOT_CHECK_ENV);
		bool skip_check = (root_check && streq(root_check, "0"));

		/* FIXME: should check client's capability set instead of UID */
		if (control->client.uid != 0 && !skip_check) {
			ret = EPERM;
			buxton_debug("Not permitted to create group '%s'\n", key->group.value);
			goto fail;
		}
	} else {
		buxton_debug("Cannot set labels in a user layer\n");
		goto fail;
	}

	backend = backend_for_layer(config, layer);
	if (!backend) {
		/* Already logged */
		goto fail;
	}

	ret = buxton_direct_get_value_for_layer(control, key, data, data_label, NULL);
	if (ret) {
		buxton_debug("Group or key does not exist\n");
		goto fail;
	}

	free(data_label->value);

	if (!buxton_string_copy(label, data_label)) {
		buxton_debug("Failed to copy data label\n");
		goto fail;
	}

	layer->uid = control->client.uid;
	ret = backend->set_value(layer, key, data, data_label);
	if (ret)
		buxton_debug("set label failed\n");

fail:
	return ret;
}

int32_t buxton_direct_create_group(BuxtonControl *control,
				_BuxtonKey *key,
				BuxtonString *label)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	BuxtonString s, l;
	_cleanup_buxton_data_ BuxtonData *data = NULL;
	_cleanup_buxton_data_ BuxtonData *group = NULL;
	_cleanup_buxton_string_ BuxtonString *dlabel = NULL;
	_cleanup_buxton_string_ BuxtonString *glabel = NULL;
	int32_t r = BUXTON_STATUS_FAILED;

	assert(control);
	assert(key);

	data = malloc0(sizeof(BuxtonData));
	if (!data)
		goto fail;
	group = malloc0(sizeof(BuxtonData));
	if (!group)
		goto fail;
	dlabel = malloc0(sizeof(BuxtonString));
	if (!dlabel)
		goto fail;
	glabel = malloc0(sizeof(BuxtonString));
	if (!glabel)
		goto fail;

	config = &control->config;

	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
		goto fail;
	}

	if (layer->type == LAYER_SYSTEM) {
		char *root_check = getenv(BUXTON_ROOT_CHECK_ENV);
		bool skip_check = (root_check && streq(root_check, "0"));

		/* FIXME: should check client's capability set instead of UID */
		if (control->client.uid != 0 && !skip_check) {
			buxton_debug("Not permitted to create group '%s'\n", key->group.value);
			r = EPERM;
			goto fail;
		}
	}

	if (buxton_direct_get_value_for_layer(control, key, group, glabel, NULL)) {
		buxton_debug("Group '%s' already exists\n", key->group.value);
		r = EEXIST;
		goto fail;
	}

	backend = backend_for_layer(config, layer);
	if (!backend) {
		/* Already logged */
		goto fail;
	}

	/* Since groups don't have a value, we create a dummy value */
	data->type = STRING;
	s = buxton_string_pack("BUXTON_GROUP_VALUE");
	if (!buxton_string_copy(&s, &data->store.d_string)) {
		buxton_debug("Failed to set group data value\n");
		goto fail;
	}

	if (label) {
		if (!buxton_string_copy(label, dlabel)) {
			buxton_debug("Failed to copy group label\n");
			goto fail;
		}
	} else {
		/* _ (floor) is our current default label */
		l = buxton_string_pack("_");
		if (!buxton_string_copy(&l, dlabel)) {
			buxton_debug("Failed to copy default group label\n");
			goto fail;
		}
	}

	layer->uid = control->client.uid;
	r = backend->set_value(layer, key, data, dlabel);
	if (!r)
		buxton_debug("create group failed\n");

fail:
	return r;
}

int32_t buxton_direct_remove_group(BuxtonControl *control,
				_BuxtonKey *key,
				BuxtonString *client_label)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	_cleanup_buxton_data_ BuxtonData *group = NULL;
	_cleanup_buxton_string_ BuxtonString *glabel = NULL;
	int32_t ret = BUXTON_STATUS_FAILED;

	assert(control);
	assert(key);

	group = malloc0(sizeof(BuxtonData));
	if (!group)
		goto fail;
	glabel = malloc0(sizeof(BuxtonString));
	if (!glabel)
		goto fail;

	config = &control->config;

	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL)
		goto fail;


	if (layer->type == LAYER_SYSTEM) {
		char *root_check = getenv(BUXTON_ROOT_CHECK_ENV);
		bool skip_check = (root_check && streq(root_check, "0"));

		/* FIXME: should check client's capability set instead of UID */
		if (control->client.uid != 0 && !skip_check) {
			buxton_debug("Not permitted to remove group '%s'\n", key->group.value);
			ret = EPERM;
			goto fail;
		}
	}

	if (!buxton_direct_get_value_for_layer(control, key, group, glabel, NULL)) {
		buxton_debug("Group '%s' doesn't exist\n", key->group.value);
		goto fail;
	}

	if (layer->type == LAYER_USER) {
		if (client_label && !buxton_check_smack_access(client_label, glabel, ACCESS_WRITE)) {
			goto fail;
		}
	}

	backend = backend_for_layer(config, layer);
	if (!backend) {
		/* Already logged */
		goto fail;
	}

	layer->uid = control->client.uid;

	ret = backend->unset_value(layer, key, NULL, NULL);
	if (ret)
		buxton_debug("remove group failed\n");

fail:
	return ret;
}

bool buxton_direct_list_keys(BuxtonControl *control,
			     BuxtonString *layer_name,
			     BuxtonArray **list)
{
	assert(control);
	assert(layer_name);

	/* Handle direct manipulation */
	BuxtonBackend *backend = NULL;
	BuxtonLayer *layer;
	BuxtonConfig *config;

	config = &control->config;
	if ((layer = hashmap_get(config->layers, layer_name->value)) == NULL) {
		return false;
	}
	backend = backend_for_layer(config, layer);
	if (!backend) {
		/* Already logged */
		return false;
	}
	layer->uid = control->client.uid;
	return backend->list_keys(layer, list);
}

bool buxton_direct_unset_value(BuxtonControl *control,
			       _BuxtonKey *key,
			       BuxtonString *label)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	_cleanup_buxton_string_ BuxtonString *data_label = NULL;
	_cleanup_buxton_string_ BuxtonString *group_label = NULL;
	_cleanup_buxton_data_ BuxtonData *d = NULL;
	_cleanup_buxton_data_ BuxtonData *g = NULL;
	_cleanup_buxton_key_ _BuxtonKey *group = NULL;

	assert(control);
	assert(key);

	group = malloc0(sizeof(_BuxtonKey));
	if (!group)
		goto fail;
	g = malloc0(sizeof(BuxtonData));
	if (!g)
		goto fail;
	group_label = malloc0(sizeof(BuxtonString));
	if (!group_label)
		goto fail;

	d = malloc0(sizeof(BuxtonData));
	if (!d)
		goto fail;
	data_label = malloc0(sizeof(BuxtonString));
	if (!data_label)
		goto fail;

	if (!buxton_copy_key_group(key, group))
		goto fail;

	if (!buxton_direct_get_value_for_layer(control, group, g, group_label, NULL)) {
		buxton_debug("Group %s for name %s missing for unset value\n", key->group.value, key->name.value);
		goto fail;
	}

	/* Access checks are not needed for direct clients, where label is NULL */
	if (label) {
		if (!buxton_check_smack_access(label, group_label, ACCESS_WRITE))
			goto fail;
		if (buxton_direct_get_value_for_layer(control, key, d, data_label, NULL)) {
			if (!buxton_check_smack_access(label, data_label, ACCESS_WRITE)) {
				goto fail;
			}
		} else {
			buxton_debug("Key %s not found, so unset fails\n", key->name.value);
			goto fail;
		}
	}

	config = &control->config;
	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL)
		return false;

	backend = backend_for_layer(config, layer);
	if (!backend) {
		/* Already logged */
		return false;
	}
	layer->uid = control->client.uid;
	return backend->unset_value(layer, key, NULL, NULL);

fail:
	return false;
}

void buxton_direct_close(BuxtonControl *control)
{
	Iterator iterator;
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonString *key;

	control->client.direct = false;

	HASHMAP_FOREACH(backend, control->config.backends, iterator) {
		destroy_backend(backend);
	}
	hashmap_free(control->config.backends);
	hashmap_free(control->config.databases);

	HASHMAP_FOREACH_KEY(layer, key, control->config.layers, iterator) {
		hashmap_remove(control->config.layers, key);
		free(layer->name.value);
		free(layer->description);
		free(layer);
	}
	hashmap_free(control->config.layers);

	control->client.direct = false;
	control->config.backends = NULL;
	control->config.databases = NULL;
	control->config.layers = NULL;
}

/*
 * Editor modelines  -	http://www.wireshark.org/tools/modelines.html
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
