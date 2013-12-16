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
#include "buxton-array.h"

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
 * 2 is the minimum number of characters in a valid SMACK label
 * 1 is the mimimum number of characters in a valid value
 */
#define BXT_MINIMUM_SIZE sizeof(BuxtonDataType) \
	+ (sizeof(uint32_t) * 2) \
	+ 2 + 1

/**
 * A control message for the wire protocol
 */
typedef enum BuxtonControlMessage{
	BUXTON_CONTROL_MIN,
	BUXTON_CONTROL_SET, /**<Set a value within Buxton */
	BUXTON_CONTROL_GET, /**<Retrieve a value from Buxton */
	BUXTON_CONTROL_UNSET, /**<Unset a value within Buxton */
	BUXTON_CONTROL_STATUS, /**<Status code follows */
	BUXTON_CONTROL_NOTIFY, /**<Register for notification */
	BUXTON_CONTROL_UNNOTIFY, /**<Opt out of notifications */
	BUXTON_CONTROL_CHANGED, /**<A key changed in Buxton */
	BUXTON_CONTROL_MAX
} BuxtonControlMessage;

/**
 * Length of valid message header
 */
#define BUXTON_MESSAGE_HEADER_LENGTH sizeof(uint32_t) \
	+ sizeof(uint32_t)
/**
 * Maximum length of valid control message
 */
#define BUXTON_MESSAGE_MAX_LENGTH 4096

/**
 * Maximum length of valid control message
 */
#define BUXTON_MESSAGE_MAX_PARAMS 16

/**
 * Serialize data internally for backend consumption
 * @param source Data to be serialized
 * @param dest Pointer to store serialized data in
 * @return a size_t value, indicating the size of serialized data
 */
size_t buxton_serialize(BuxtonData *source, uint8_t **dest);

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
 * @param list An array of BuxtonData's to be serialized
 * @return a size_t, 0 indicates failure otherwise size of dest
 */
size_t buxton_serialize_message(uint8_t **dest,
				BuxtonControlMessage message,
				BuxtonArray *list);

/**
 * Deserialize the given data into an array of BuxtonData structs
 * @param data The source data to be deserialized
 * @param message An empty pointer that will be set to the message type
 * @param size The size of the data being deserialized
 * @param list A pointer that will be filled out as an array of BuxtonData structs
 * @return the length of the array, or 0 if deserialization failed
 */
size_t buxton_deserialize_message(uint8_t *data,
				  BuxtonControlMessage *message,
				  size_t size, BuxtonData **list);

/**
 * Get size of a buxton message data stream
 * @param data The source data stream
 * @param size The size of the data stream (from read)
 * @return a size_t length of the complete message or 0
 */
size_t buxton_get_message_size(uint8_t *data, size_t size);

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
