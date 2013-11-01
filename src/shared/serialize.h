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
 * \file serialize.h Internal header
 * This file is used internally by buxton to provide serialization
 * functionality
 */
#pragma once

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include <stdint.h>

#include "bt-daemon.h"

/**
 * Magic for Buxton messages
 */
#define BUXTON_CONTROL_CODE 0x672

/**
 * Location of size in serialized message data
 */
#define BUXTON_LENGTH_OFFSET sizeof(uint32_t)

/**
 * Minimum size of serialized BuxtonData
 */
#define BXT_MINIMUM_SIZE sizeof(BuxtonDataType) + (sizeof(int)*2)

/**
 * A control message for the wire protocol
 */
typedef enum BuxtonControlMessage{
	BUXTON_CONTROL_SET, /**<Set a value within Buxton */
	BUXTON_CONTROL_GET, /**<Retrieve a value from Buxton */
	BUXTON_CONTROL_STATUS, /**<Status code follows */
	BUXTON_CONTROL_MAX
} BuxtonControlMessage;

/**
 * Minimum length of valid control message
 */
#define BUXTON_CONTROL_LENGTH sizeof(uint32_t) \
	+ sizeof(size_t)
/**
 * Maximum length of valid control message
 */
#define BUXTON_CONTROL_LENGTH_MAX 4096

/**
 * Serialize data internally for backend consumption
 * @param source Data to be serialized
 * @param dest Pointer to store serialized data in
 * @return a boolean value, indicating success of the operation
 */
bool buxton_serialize(BuxtonData *source, uint8_t** dest);

/**
 * Deserialize internal data for client consumption
 * @param source Serialized data pointer
 * @param dest A pointer where the deserialize data will be stored
 * @return a boolean value, indicating success of the operation
 */
bool buxton_deserialize(uint8_t *source, BuxtonData *dest);

/**
 * Serialize an internal buxton message for wire communication
 * @param dest Pointer to store serialized message in
 * @param message The type of message to be serialized
 * @param n_params Number of parameters in va_args list
 * @param ... Variable argument list of BuxtonData pointers
 * @return a size_t, 0 indicates failure otherwise size of dest
 */
size_t buxton_serialize_message(uint8_t **dest, BuxtonControlMessage message,
			      unsigned int n_params, ...);

/**
 * Deserialize the given data into an array of BuxtonData structs
 * @param data The source data to be deserialized
 * @param message An empty pointer that will be set to the message type
 * @param size The size of the data being deserialized
 * @param list A pointer that will be filled out as an array of BuxtonData structs
 * @return the length of the array, or a negative value if deserialization failed
 */
int buxton_deserialize_message(uint8_t *data, BuxtonControlMessage *message,
			       int size, BuxtonData **list);

/**
 * Get size of a buxton message data stream
 * @param data The source data stream
 * @param size The size of the data stream (from read)
 * @return size_t The length of the complete message or 0
 */
size_t buxton_get_message_size(uint8_t *data, int size);

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
