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

typedef struct mx_protocol_char {
    ssize_t (*read)(mx_device_t* dev, void* buf, size_t count);
    ssize_t (*write)(mx_device_t* dev, const void* buf, size_t count);
    ssize_t (*ioctl)(mx_device_t* dev, uint32_t op,
                     const void* in_buf, size_t in_len,
                     void* out_buf, size_t out_len);
} mx_protocol_char_t;
