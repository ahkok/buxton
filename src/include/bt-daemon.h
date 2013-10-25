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
 * \file bt-daemon.h Buxton public header
 *
 * This is the public part of libbuxton
 *
 * \mainpage Buxton
 * \link bt-daemon.h Public API
 * \endlink - API listing for libbuxton
 * \copyright Copyright (C) 2013 Intel corporation
 * \par License
 * GNU Lesser General Public License 2.1
 */



#pragma once

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>

#if (__GNUC__ >= 4)
/* Export symbols */
#    define _bx_export_ __attribute__ ((visibility("default")))
#else
#  define _bx_export_
#endif

/**
 * Used to communicate with Buxton
 */
typedef struct BuxtonClient {
	int fd; /**<The file descriptor for the connection */
	bool direct; /**<Only used for direction connections */
	pid_t pid; /**<Process ID, used within libbuxton */
} BuxtonClient;

/**
 * Possible data types for use in Buxton
 */
typedef enum BuxtonDataType {
	STRING, /**<Represents type of a string value */
	BOOLEAN, /**<Represents type of a boolean value */
	FLOAT, /**<Represents type of a float value */
	INT, /**<Represents type of an integer value */
	DOUBLE, /**<Represents type of a double value */
	LONG, /**<Represents type of a long value */
	BUXTON_TYPE_MAX
} BuxtonDataType;

/**
 * Stores values in Buxton, may have only one value
 */
typedef union BuxtonDataStore {
	char *d_string; /**<Stores a string value */
	bool d_boolean; /**<Stores a boolean value */
	float d_float; /**<Stores a float value */
	int d_int; /**<Stores an integer value */
	double d_double; /**<Stores a double value */
	long d_long; /**<Stores a long value */
} BuxtonDataStore;

/**
 * Represents data in Buxton
 *
 * In Buxton we operate on all data using BuxtonData, for both set and
 * get operations. The type must be set to the type of value being set
 * in the BuxtonDataStore
 */
typedef struct BuxtonData {
	BuxtonDataType type; /**<Type of data stored */
	BuxtonDataStore store;/**<Contains one value, correlating to type */
} BuxtonData;

/* Buxton API Methods */

/**
 * Open a connection to Buxton
 * @param client A BuxtonClient
 * @return A boolean value, indicating success of the operation
 */
_bx_export_ bool buxton_client_open(BuxtonClient *client);

/**
 * Close the connection to Buxton
 * @param client A BuxtonClient
 */
_bx_export_ void buxton_client_close(BuxtonClient *client);

/**
 * Set a value within Buxton
 * @param client An open client connection
 * @param layer The layer to manipulate
 * @param key The key name
 * @param data A struct containing the data to set
 * @return A boolean value, indicating success of the operation
 */
_bx_export_ bool buxton_client_set_value(BuxtonClient *client, const char *layer, const char *key, BuxtonData *data);

/**
 * Retrieve a value from Buxton
 * @param client An open client connection
 * @param key The key to retrieve
 * @param data An empty BuxtonData, where data is stored
 * @return A boolean value, indicating success of the operation
 */
_bx_export_ bool buxton_client_get_value(BuxtonClient *client, const char *key, BuxtonData *data);

/**
 * Retrieve a value from Buxton
 * @param client An open client connection
 * @param layer The layer where the key is set
 * @param key The key to retrieve
 * @param data An empty BuxtonData, where data is stored
 * @return A boolean value, indicating success of the operation
 */
_bx_export_ bool buxton_client_get_value_for_layer(BuxtonClient *client, const char *layer, const char *key, BuxtonData *data);

/**
 * Open a direct connection to Buxton
 *
 * @param client The client struct will be used throughout the life of Buxton operations
 * @return a boolean value, indicating success of the operation
 */
_bx_export_ bool buxton_direct_open(BuxtonClient *client);

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
