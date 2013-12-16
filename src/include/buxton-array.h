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

#pragma once

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <bt-daemon.h>

/**
 * A dynamic array
 */
typedef struct BuxtonArray {
	void **data; /**<Dynamic array contents */
	uint len; /**<Length of the array */
} BuxtonArray;

/**
 * Valid function prototype for 'free' method
 * @param p Pointer to free
 */
typedef void (*buxton_free_func) (void* p);

/**
 * Create a new BuxtonArray
 * @returns BuxtonArray a newly allocated BuxtonArray
 */
BuxtonArray *buxton_array_new(void);

/**
 * Append data to BuxtonArray
 * @param array Valid BuxtonArray
 * @param data Pointer to add to this array
 * @returns bool true if the data was added to the array
 */
bool buxton_array_add(BuxtonArray *array,
		      void *data);

/**
 * Free an array, and optionally its members
 * @param array valid BuxtonArray reference
 * @param free_func Function to call to free all members, or NULL to leave allocated
 */
void buxton_array_free(BuxtonArray **array,
		       buxton_free_func free_method);


/**
 * Retrieve a BuxtonData from the array by index
 * @param array valid BuxtonArray reference
 * @param index index of the element in the array
 * @return a BuxtonData refered to by index, or NULL
 */
BuxtonData *buxton_array_get(BuxtonArray *array, uint16_t index);

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
