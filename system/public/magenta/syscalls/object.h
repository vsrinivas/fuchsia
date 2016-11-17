// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// Valid topics for mx_object_get_info.
typedef enum {
    MX_INFO_HANDLE_VALID = 1,
    MX_INFO_HANDLE_BASIC,           // mx_info_handle_basic_t[1]
    MX_INFO_PROCESS,                // mx_info_process_t[1]
    MX_INFO_PROCESS_THREADS,        // mx_koid_t[n]
    MX_INFO_RESOURCE_CHILDREN,      // mx_rrec_t[n]
    MX_INFO_RESOURCE_RECORDS,       // mx_rrec_t[n]
} mx_object_info_topic_t;

typedef enum {
    MX_OBJ_TYPE_NONE                = 0,
    MX_OBJ_TYPE_PROCESS             = 1,
    MX_OBJ_TYPE_THREAD              = 2,
    MX_OBJ_TYPE_VMEM                = 3,
    MX_OBJ_TYPE_CHANNEL             = 4,
    MX_OBJ_TYPE_EVENT               = 5,
    MX_OBJ_TYPE_IOPORT              = 6,
    MX_OBJ_TYPE_DATA_PIPE_PRODUCER  = 7,
    MX_OBJ_TYPE_DATA_PIPE_CONSUMER  = 8,
    MX_OBJ_TYPE_INTERRUPT           = 9,
    MX_OBJ_TYPE_IOMAP               = 10,
    MX_OBJ_TYPE_PCI_DEVICE          = 11,
    MX_OBJ_TYPE_LOG                 = 12,
    MX_OBJ_TYPE_WAIT_SET            = 13,
    MX_OBJ_TYPE_SOCKET              = 14,
    MX_OBJ_TYPE_RESOURCE            = 15,
    MX_OBJ_TYPE_EVENT_PAIR          = 16,
    MX_OBJ_TYPE_JOB                 = 17,
    MX_OBJ_TYPE_LAST
} mx_obj_type_t;

typedef enum {
    MX_OBJ_PROP_NONE            = 0,
    MX_OBJ_PROP_WAITABLE        = 1,
} mx_obj_props_t;

typedef struct mx_info_handle_basic {
    mx_koid_t koid;
    mx_rights_t rights;
    uint32_t type;                // mx_obj_type_t;
    uint32_t props;               // mx_obj_props_t;
} mx_info_handle_basic_t;

typedef struct mx_info_process {
    int return_code;
} mx_info_process_t;


// Object properties.

// Argument is MX_POLICY_BAD_HANDLE_... (below, uint32_t).
#define MX_PROP_BAD_HANDLE_POLICY           1u
// Argument is a uint32_t.
#define MX_PROP_NUM_STATE_KINDS             2u
// Argument is an mx_size_t.
#define MX_PROP_DATAPIPE_READ_THRESHOLD     3u
// Argument is an mx_size_t.
#define MX_PROP_DATAPIPE_WRITE_THRESHOLD    4u

// Policies for MX_PROP_BAD_HANDLE_POLICY:
#define MX_POLICY_BAD_HANDLE_IGNORE         0u
#define MX_POLICY_BAD_HANDLE_LOG            1u
#define MX_POLICY_BAD_HANDLE_EXIT           2u

__END_CDECLS
