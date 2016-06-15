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

#include <stddef.h>

#include <magenta/types.h>

mx_handle_t mxu_read_handle(mx_handle_t h);

mx_status_t mxu_blocking_read(mx_handle_t h, void* data, size_t len);

mx_status_t mxu_blocking_read_h(mx_handle_t h, void* data, size_t len, mx_handle_t* out);
