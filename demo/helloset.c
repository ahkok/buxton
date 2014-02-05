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
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "buxton.h"

void set_cb(BuxtonResponse response, void *data)
{
	BuxtonKey key;

	if (response_status(response) != BUXTON_STATUS_OK) {
		printf("Failed to set value\n");
		return;
	}

	key = response_key(response);
	printf("Set value for key %s\n", buxton_get_name(key));
}

int main(void)
{
	BuxtonClient client;
	BuxtonKey key;
	struct pollfd pfd[1];
	int r;
	int fd;
	int32_t set;

	if ((fd = buxton_client_open(&client)) < 0) {
		printf("couldn't connect\n");
		return -1;
	}

	key = buxton_make_key("hello", "test", "base", INT32);
	if (!key)
		return -1;

	set = 10;

	if (!buxton_client_set_value(client, key, &set, set_cb,
				     NULL, false)) {
		printf("set call failed to run\n");
		return -1;
	}

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	r = poll(pfd, 1, 5000);

	if (r <= 0) {
		printf("poll error\n");
		return -1;
	}

	if (!buxton_client_handle_response(client)) {
		printf("bad response from daemon\n");
		return -1;
	}

	buxton_free_key(key);
	buxton_client_close(client);
	return 0;
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
