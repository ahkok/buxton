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
 * \file core/main.c Buxton daemon
 *
 * This file provides the buxton daemon
 */
#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <systemd/sd-daemon.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <attr/xattr.h>

#include "bt-daemon.h"
#include "daemon.h"
#include "list.h"
#include "log.h"
#include "protocol.h"
#include "smack.h"
#include "util.h"

static BuxtonDaemon self;

/**
 * Entry point into bt-daemon
 * @param argc Number of arguments passed
 * @param argv An array of string arguments
 * @returns EXIT_SUCCESS if the operation succeeded, otherwise EXIT_FAILURE
 */
int main(int argc, char *argv[])
{
	int fd;
	int smackfd = -1;
	socklen_t addr_len;
	struct sockaddr_un remote;
	int descriptors;
	int ret;
	bool manual_start = false;

	if (USE_SMACK) {
		if (!buxton_cache_smack_rules())
			exit(EXIT_FAILURE);
		smackfd = buxton_watch_smack_rules();
		if (smackfd < 0)
			exit(EXIT_FAILURE);
	}

	self.nfds_alloc = 0;
	self.accepting_alloc = 0;
	self.nfds = 0;
	self.buxton.direct = true;
	self.set_value = &set_value;
	self.get_value = &get_value;
	self.register_notification = &register_notification;
	self.buxton.uid = geteuid();
	if (!buxton_direct_open(&self.buxton))
		exit(EXIT_FAILURE);

	/* For client notifications */
	self.notify_mapping = hashmap_new(string_hash_func, string_compare_func);
	/* Store a list of connected clients */
	LIST_HEAD_INIT(client_list_item, self.client_list);

	descriptors = sd_listen_fds(0);
	if (descriptors < 0) {
		buxton_log("sd_listen_fds: %m\n");
		exit(EXIT_FAILURE);
	} else if (descriptors == 0) {
		/* Manual invocation */
		manual_start = true;
		union {
			struct sockaddr sa;
			struct sockaddr_un un;
		} sa;

		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0) {
			buxton_log("socket(): %m\n");
			exit(EXIT_FAILURE);
		}

		memset(&sa, 0, sizeof(sa));
		sa.un.sun_family = AF_UNIX;
		strncpy(sa.un.sun_path, BUXTON_SOCKET, sizeof(sa.un.sun_path) - 1);
		sa.un.sun_path[sizeof(sa.un.sun_path)-1] = 0;

		ret = unlink(sa.un.sun_path);
		if (ret == -1 && errno != ENOENT) {
			exit(EXIT_FAILURE);
		}

		if (bind(fd, &sa.sa, sizeof(sa)) < 0) {
			buxton_log("bind(): %m\n");
			exit(EXIT_FAILURE);
		}

		chmod(sa.un.sun_path, 0666);

		if (listen(fd, SOMAXCONN) < 0) {
			buxton_log("listen(): %m\n");
			exit(EXIT_FAILURE);
		}
		add_pollfd(&self, fd, POLLIN | POLLPRI, true);
	} else {
		/* systemd socket activation */
		for (fd = SD_LISTEN_FDS_START + 0; fd < SD_LISTEN_FDS_START + descriptors; fd++) {
			if (sd_is_fifo(fd, NULL)) {
				add_pollfd(&self, fd, POLLIN, false);
				buxton_debug("Added fd %d type FIFO\n", fd);
			} else if (sd_is_socket_unix(fd, SOCK_STREAM, -1, BUXTON_SOCKET, 0)) {
				add_pollfd(&self, fd, POLLIN | POLLPRI, true);
				buxton_debug("Added fd %d type UNIX\n", fd);
			} else if (sd_is_socket(fd, AF_UNSPEC, 0, -1)) {
				add_pollfd(&self, fd, POLLIN | POLLPRI, true);
				buxton_debug("Added fd %d type SOCKET\n", fd);
			}
		}
	}

	if (USE_SMACK) {
		/* add Smack rule fd to pollfds */
		add_pollfd(&self, smackfd, POLLIN | POLLPRI, false);
	}

	buxton_log("%s: Started\n", argv[0]);

	/* Enter loop to accept clients */
	for (;;) {
		ret = poll(self.pollfds, self.nfds, -1);

		if (ret < 0) {
			buxton_log("poll(): %m\n");
			break;
		}
		if (ret == 0)
			continue;

		for (nfds_t i=0; i<self.nfds; i++) {
			client_list_item *cl = NULL;
			char discard[256];

			if (self.pollfds[i].revents == 0)
				continue;

			if (self.pollfds[i].fd == -1) {
				/* TODO: Remove client from list  */
				buxton_debug("Removing / Closing client for fd %d\n", self.pollfds[i].fd);
				del_pollfd(&self, i);
				continue;
			}

			if (USE_SMACK) {
				if (self.pollfds[i].fd == smackfd) {
					if (!buxton_cache_smack_rules())
						exit(EXIT_FAILURE);
					buxton_log("Reloaded Smack access rules\n");
					/* discard inotify data itself */
					while (read(smackfd, &discard, 256) == 256);
					continue;
				}
			}

			if (self.accepting[i] == true) {
				int fd;
				int on = 1;

				addr_len = sizeof(remote);

				if ((fd = accept(self.pollfds[i].fd,
				    (struct sockaddr *)&remote, &addr_len)) == -1) {
					buxton_log("accept(): %m\n");
					break;
				}

				buxton_debug("New client fd %d connected through fd %d\n", fd, self.pollfds[i].fd);

				cl = malloc0(sizeof(client_list_item));
				if (!cl)
					exit(EXIT_FAILURE);

				LIST_INIT(client_list_item, item, cl);

				cl->fd = fd;
				cl->cred = (struct ucred) {0, 0, 0};
				LIST_PREPEND(client_list_item, item, self.client_list, cl);

				/* poll for data on this new client as well */
				add_pollfd(&self, cl->fd, POLLIN | POLLPRI, false);

				/* Mark our packets as high prio */
				if (setsockopt(cl->fd, SOL_SOCKET, SO_PRIORITY, &on, sizeof(on)) == -1)
					buxton_log("setsockopt(SO_PRIORITY): %m\n");

				/* check if this is optimal or not */
				break;
			}

			assert(self.accepting[i] == 0);
			if (USE_SMACK)
				assert(self.pollfds[i].fd != smackfd);

			/* handle data on any connection */
			/* TODO: Replace with hash table lookup */
			LIST_FOREACH(item, cl, self.client_list)
				if (self.pollfds[i].fd == cl->fd)
					break;

			assert(cl);
			handle_client(&self, cl, i);
		}
	}

	buxton_log("%s: Closing all connections\n", argv[0]);

	if (manual_start)
		unlink(BUXTON_SOCKET);
	for (int i = 0; i < self.nfds; i++) {
		close(self.pollfds[i].fd);
	}
	for (client_list_item *i = self.client_list; i;) {
		client_list_item *j = i->item_next;
		free(i);
		i = j;
	}
	hashmap_free(self.notify_mapping);
	buxton_client_close(&self.buxton);
	return EXIT_SUCCESS;
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
