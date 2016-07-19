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

#include <stdbool.h>

#include <magenta/types.h>

// Waits for an incoming echo request on message pipe |handle|,
// parses the message, sends a reply on |handle|. Returns false if either
// message pipe handle is closed or any error occurs. Returns true if a reply is
// successfully sent.
bool serve_echo_request(mx_handle_t handle);
