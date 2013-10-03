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

#define _GNU_SOURCE

#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>

#include "../shared/list.h"
#include "bt-daemon.h"

#ifndef btdaemonh_private
#define btdaemonh_private

#define TIMEOUT 5000

typedef struct client_list_item {
	LIST_FIELDS(struct client_list_item, item);

	int client_socket;

	struct ucred credentials;
} client_list_item;

/* Module related code */
typedef int (*module_value_func) (const char *resource, const char *key, BuxtonData *data);

typedef struct BuxtonBackend {
	void *module;

	module_value_func set_value;
	module_value_func get_value;

} BuxtonBackend;

typedef int (*module_init_func) (BuxtonBackend *backend);
typedef void (*module_destroy_func) (BuxtonBackend *backend);

/* Initialise a backend module */
bool init_backend(const char *name, BuxtonBackend *backend);

/* Obtain the current backend for the given layer */
BuxtonBackend *backend_for_layer(const char *layer);

/* Directly manipulate buxton without socket connection */
_bx_export_ bool buxton_direct_open(BuxtonClient *client);

#endif /* btdaemonh_private */

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
