// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
