// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/status.h>

const char* _mx_status_get_string(mx_status_t status) {

    switch (status) {
#define FUCHSIA_ERROR(status, id) \
    case ERR_##status:            \
        return #status;
#include <magenta/fuchsia-types.def>
#undef FUCHSIA_ERROR

    default:
        return "No such mx_status_t";
    }
}

__typeof(mx_status_get_string) mx_status_get_string
    __attribute__((weak, alias("_mx_status_get_string")));
