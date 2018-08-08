// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STATUS_H
#define STATUS_H

#include "magma_common_defs.h"

namespace magma {

class Status {
public:
    Status(magma_status_t status) : status_(status) {}

    magma_status_t get() const { return status_; }

    bool ok() const { return status_ == MAGMA_STATUS_OK; }

    explicit operator bool() const { return ok(); }

private:
    magma_status_t status_;
};
} // namespace magma

#endif // STATUS_H
