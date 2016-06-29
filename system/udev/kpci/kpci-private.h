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
#include <ddk/binding.h>
#include <magenta/types.h>

typedef struct kpci_device {
    mx_device_t device;
    mx_handle_t handle;
    uint32_t index;
    mx_pcie_get_nth_info_t info;
    mx_device_prop_t props[7];
} kpci_device_t;

#define get_kpci_device(dev) containerof(dev, kpci_device_t, device)
