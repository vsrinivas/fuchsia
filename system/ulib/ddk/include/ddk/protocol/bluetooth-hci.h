// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

typedef struct bluetooth_hci_protocol {
    // returns message pipe for command & event packets
    mx_handle_t (*get_control_pipe)(mx_device_t* device);
    // returns message pipe for ACL data
    mx_handle_t (*get_acl_pipe)(mx_device_t* device);
} bluetooth_hci_protocol_t;

__END_CDECLS;
