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

#pragma GCC visibility push(hidden)

#include <magenta/types.h>
#include <stddef.h>
#include <stdint.h>

struct bootfs {
    const uint8_t* contents;
    size_t len;
};

void bootfs_mount(mx_handle_t log, mx_handle_t vmo, struct bootfs *fs);
void bootfs_unmount(mx_handle_t log, struct bootfs *fs);

mx_handle_t bootfs_open(mx_handle_t log, struct bootfs *fs,
                        const char* filename);

#pragma GCC visibility pop
