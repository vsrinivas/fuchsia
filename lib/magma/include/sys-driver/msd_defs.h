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

#ifndef _MSD_DEFS_H_
#define _MSD_DEFS_H_

#include "common/magma_defs.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint32_t msd_client_id;

struct msd_exec_buffer {
    uint32_t placeholder;
};

#if defined(__cplusplus)
}
#endif

#endif /* _MSD_DEFS_H_ */
