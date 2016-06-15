// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <magenta/types.h>

typedef struct bluetooth_hci_protocol {
    // returns message pipe for command & event packets
    mx_handle_t (* get_control_pipe)(mx_device_t* device);
    // returns message pipe for ACL data
    mx_handle_t (* get_acl_pipe)(mx_device_t* device);
} bluetooth_hci_protocol_t;
