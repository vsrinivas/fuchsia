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

#include "magma_sys_driver.h"

void msd_driver_destroy(msd_driver* arch) {}

msd_device* msd_driver_create_device(msd_driver* arch, void* device) { return nullptr; }

void msd_driver_destroy_device(msd_device*) {}

int msd_device_open(msd_device* dev, ClientId client_id) { return 0; }

int msd_device_close(msd_device* dev, ClientId client_id) { return 0; }

uint32_t msd_device_get_id(msd_device* dev) { return 0; }
