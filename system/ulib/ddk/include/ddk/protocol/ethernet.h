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

#include <ddk/driver.h>
#include <hw/usb.h>

typedef struct ethernet_protocol {
    mx_status_t (*send)(mx_device_t* device, const void* buffer, size_t length);
    // returns length received, or error
    mx_status_t (*recv)(mx_device_t* device, void* buffer, size_t length);
    mx_status_t (*get_mac_addr)(mx_device_t* device, uint8_t* out_addr);
    mx_status_t (*is_online)(mx_device_t* device);
    size_t (*get_mtu)(mx_device_t* device);
} ethernet_protocol_t;
