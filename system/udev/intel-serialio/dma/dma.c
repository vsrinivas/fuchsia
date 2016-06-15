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

#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/types.h>

#include <intel-serialio/serialio.h>

mx_status_t intel_broadwell_serialio_bind_dma(mx_driver_t* drv,
                                              mx_device_t* dev) {
    // Not implemented yet.
    return ERR_NOT_IMPLEMENTED;
}
