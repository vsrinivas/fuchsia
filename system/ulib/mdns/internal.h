// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.t

#pragma once

#include <stdbool.h>

#include "mdns/mdns.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 iff type is one of the supported RR_TYPE* constants.
bool is_valid_rr_type(uint16_t type);

// Returns 0 iff type is one of the supported RR_CLASS* constants.
bool is_valid_rr_class(uint16_t clazz);

// Appends a resource record to the list starting at rrsptr and using the given
// property values. Returns the number of records in the linked list after
// insertion.  If memory cannot be allocated for the new record, a negative
// value is returned with errno set to ENOMEM.  name is expected to be a NULL
// terminated string. type and clazz must be one of the RR_TYPE and RR_CLASS
// constants, respectively. Otherwise, a negative value is returned and errno is
// set to EINVAL.
//
// Returns 0 on success.
int mdns_add_rr(mdns_rr** rrsptr,
                char* name,
                uint16_t type,
                uint16_t clazz,
                uint8_t* rdata,
                uint16_t rdlength,
                uint32_t ttl);

#ifdef __cplusplus
} // extern "C"
#endif
