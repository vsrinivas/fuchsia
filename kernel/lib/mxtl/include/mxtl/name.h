// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/spinlock.h>

#include <magenta/types.h>
#include <magenta/thread_annotations.h>

// A class for managing names of kernel objects. Since we don't want
// unbounded lengths, the constructor and setter perform
// truncation. Names include the trailing NUL as part of their
// MX_MAX_NAME_LEN-sized buffer.
class Name {
public:
    Name() {}
    Name(const char* name, size_t len) {
        set(name, len);
    }

    ~Name() = default;

    void get(char out_name[MX_MAX_NAME_LEN]) const;
    mx_status_t set(const char* name, size_t len);

private:
    // These Names are often included for diagnostic purposes, and
    // access to the Name might be made under various other locks or
    // in interrupt context. So we use a spinlock to serialize.
    mutable SpinLock lock_;
    // This includes the trailing NUL.
    char name_[MX_MAX_NAME_LEN] TA_GUARDED(lock_) = {};
};
