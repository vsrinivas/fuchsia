//
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
//
#pragma once

#include <ddk/driver.h>
#include <stddef.h>
#include <sys/types.h>

/* TPM IOCTL commands */
enum {
    TPM_IOCTL_SAVE_STATE = 0,
};

typedef struct mx_protocol_tpm {
    ssize_t (*get_random)(mx_device_t *dev, void *buf, size_t count);
    mx_status_t (*save_state)(mx_device_t *dev);
} mx_protocol_tpm_t;
