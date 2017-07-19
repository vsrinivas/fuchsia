// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <magenta/types.h>
#include <magenta/mdi.h>

__BEGIN_CDECLS;

typedef struct mdi_node_ref {
    const mdi_node_t* node;
    uint32_t siblings_count;        // number of siblings following node in list
    const void* siblings_end;       // pointer to end of remaining siblings
} mdi_node_ref_t;

static inline bool mdi_valid(const mdi_node_ref_t* ref) {
    return ref->node != NULL;
}

// takes pointer to MDI data and returns reference to MDI root node
mx_status_t mdi_init(const void* mdi_data, size_t length, mdi_node_ref_t* out_ref);

// returns the type of a node
static inline mdi_id_t mdi_id(const mdi_node_ref_t* ref) {
    return ref->node->id;
}

// returns the type of a node
static inline mdi_type_t mdi_node_type(const mdi_node_ref_t* ref) {
    return MDI_ID_TYPE(ref->node->id);
}

// node value accessors
mx_status_t mdi_node_uint8(const mdi_node_ref_t* ref, uint8_t* out_value);
mx_status_t mdi_node_int32(const mdi_node_ref_t* ref, int32_t* out_value);
mx_status_t mdi_node_uint32(const mdi_node_ref_t* ref, uint32_t* out_value);
mx_status_t mdi_node_uint64(const mdi_node_ref_t* ref, uint64_t* out_value);
mx_status_t mdi_node_boolean(const mdi_node_ref_t* ref, bool* out_value);
const char* mdi_node_string(const mdi_node_ref_t* ref);

// array element accessors
const void* mdi_array_values(const mdi_node_ref_t* ref);
uint32_t mdi_array_length(const mdi_node_ref_t* ref);
mx_status_t mdi_array_uint8(const mdi_node_ref_t* ref, uint8_t index, uint8_t* out_value);
mx_status_t mdi_array_int32(const mdi_node_ref_t* ref, uint32_t index, int32_t* out_value);
mx_status_t mdi_array_uint32(const mdi_node_ref_t* ref, uint32_t index, uint32_t* out_value);
mx_status_t mdi_array_uint64(const mdi_node_ref_t* ref, uint32_t index, uint64_t* out_value);
mx_status_t mdi_array_boolean(const mdi_node_ref_t* ref, uint32_t index, bool* out_value);

// list traversal
mx_status_t mdi_first_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref);
mx_status_t mdi_next_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref);
uint32_t mdi_child_count(const mdi_node_ref_t* ref);
mx_status_t mdi_find_node(const mdi_node_ref_t* ref, mdi_id_t id, mdi_node_ref_t* out_ref);

#define mdi_each_child(parent, child) \
    for (mdi_first_child(parent, child); mdi_valid(child); mdi_next_child(child, child))

__END_CDECLS;
