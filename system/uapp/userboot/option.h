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

enum option {
    OPTION_FILENAME,
#define OPTION_FILENAME_STRING "userboot"
#define OPTION_FILENAME_DEFAULT "bin/devmgr"
    OPTION_SHUTDOWN,
#define OPTION_SHUTDOWN_STRING "userboot.shutdown"
#define OPTION_SHUTDOWN_DEFAULT NULL
    OPTION_MAX
};

struct options {
    const char* value[OPTION_MAX];
};

void parse_options(mx_handle_t log, struct options *o, char** strings);

#pragma GCC visibility pop
