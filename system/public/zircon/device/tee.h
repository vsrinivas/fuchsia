// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define IOCTL_TEE_GET_DESCRIPTION \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TEE, 0)

#define IOCTL_TEE_OPEN_SESSION \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TEE, 1)

#define TEE_IOCTL_UUID_SIZE 16
#define TEE_IOCTL_MAX_PARAMS 4

typedef struct tee_revision {
    uint32_t major;
    uint32_t minor;
} tee_revision_t;

typedef struct tee_ioctl_description {
    uint8_t os_uuid[TEE_IOCTL_UUID_SIZE];
    tee_revision_t os_revision;
    bool is_global_platform_compliant;
} tee_ioctl_description_t;

// ssize_t ioctl_tee_get_description(int fd, tee_ioctl_description_t* out);
IOCTL_WRAPPER_OUT(ioctl_tee_get_description, IOCTL_TEE_GET_DESCRIPTION, tee_ioctl_description_t);

// TODO(rjascani): Eventually, this should evolve into a generic parameter set. The possible
// parameters required to open a communication channel with a Trusted Application will vary
// depending on the Trusted OS. This will be easier to accomplish once we've migrated to FIDL.
typedef uint32_t tee_ioctl_param_type_t;
#define TEE_PARAM_TYPE_NONE          ((tee_ioctl_param_type_t)0)
#define TEE_PARAM_TYPE_VALUE_INPUT   ((tee_ioctl_param_type_t)1)
#define TEE_PARAM_TYPE_VALUE_OUTPUT  ((tee_ioctl_param_type_t)2)
#define TEE_PARAM_TYPE_VALUE_INOUT   ((tee_ioctl_param_type_t)3)
#define TEE_PARAM_TYPE_MEMREF_INPUT  ((tee_ioctl_param_type_t)4)
#define TEE_PARAM_TYPE_MEMREF_OUTPUT ((tee_ioctl_param_type_t)5)
#define TEE_PARAM_TYPE_MEMREF_INOUT  ((tee_ioctl_param_type_t)6)

typedef struct tee_ioctl_param {
    tee_ioctl_param_type_t type;
    uint64_t a;
    uint64_t b;
    uint64_t c;
} tee_ioctl_param_t;

// TODO(rjascani): Currently, the identifiers for trusted apps are UUIDs like they are expected to
// be for OP-TEE. If we support more TEEs, the identifier for applications might change. So for now,
// the identifier is an array of 16 bytes, which is enough to hold a UUID. In the future, that'll
// likely need to be a longer string.
typedef struct tee_ioctl_session_request {
    uint8_t trusted_app[TEE_IOCTL_UUID_SIZE];
    uint8_t client_app[TEE_IOCTL_UUID_SIZE];
    uint32_t client_login;
    uint32_t cancel_id;
    size_t num_params;
    tee_ioctl_param_t params[TEE_IOCTL_MAX_PARAMS];
} tee_ioctl_session_request_t;

typedef struct tee_ioctl_session {
    uint32_t return_code;
    uint32_t return_origin;
    uint32_t session_id;
} tee_ioctl_session_t;

// ssize_t ioctl_tee_open_session(int fd,
//                                const tee_ioctl_session_request_t* session_request,
//                                tee_ioctl_session_t* out_session)
IOCTL_WRAPPER_INOUT(ioctl_tee_open_session, IOCTL_TEE_OPEN_SESSION,
                    tee_ioctl_session_request_t, tee_ioctl_session_t);

__END_CDECLS
