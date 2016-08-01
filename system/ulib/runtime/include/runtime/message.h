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
#include <system/compiler.h>

__BEGIN_CDECLS

#pragma GCC visibility push(hidden)

// Examine the next message to be read from the pipe, and yield
// the data size and number of handles in that message.
mx_status_t mxr_message_size(mx_handle_t msg_pipe,
                             uint32_t* nbytes, uint32_t* nhandles);

#pragma GCC visibility pop

__END_CDECLS
