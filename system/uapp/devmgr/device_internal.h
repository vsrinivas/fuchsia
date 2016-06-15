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

#include <ddk/device.h>

#define DEV_FLAG_PROTOCOL 1
// device represents a protocol

#define DEV_FLAG_DEAD 2
// Device has been removed and is waiting for refcount zero

#define DEV_FLAG_UNBINDABLE 4
// Drivers are not allowed to bind to this device

#define DEV_FLAG_REMOTE 8
// This driver is not local to devmgr

#define DEV_FLAG_BUSY 16
// Device manager is actively processing this device

mx_status_t device_open(mx_device_t* dev, uint32_t flags);
mx_status_t device_close(mx_device_t* dev);
