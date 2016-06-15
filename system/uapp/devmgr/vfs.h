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
#include <mxio/vfs.h>

void vfs_init(vnode_t* root);

// generate mxremoteio handles
mx_handle_t vfs_create_root_handle(void);
mx_handle_t vfs_create_handle(vnode_t* vn);

mx_status_t vfs_open_handles(mx_handle_t* hnds, uint32_t* ids, uint32_t arg,
                             const char* path, uint32_t flags);
