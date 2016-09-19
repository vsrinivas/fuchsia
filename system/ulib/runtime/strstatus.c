// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/status.h>

const char* mx_strstatus(mx_status_t status) {

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
