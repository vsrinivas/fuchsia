// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <string.h>

#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>

#include <magenta/types.h>
#include <magenta/thread_annotations.h>

#include <mxtl/algorithm.h>

namespace mxtl {

// A class for managing names of kernel objects. Since we don't want
// unbounded lengths, the constructor and setter perform
// truncation. Names include the trailing NUL as part of their
// Size-sized buffer.
template <size_t Size>
class Name {
public:
    Name() {}
    Name(const char* name, size_t len) {
        set(name, len);
    }

    ~Name() = default;

    void get(size_t out_len, char *out_name) const {
        AutoSpinLock lock(lock_);
        memcpy(out_name, name_, min(out_len, Size));
    }

    mx_status_t set(const char* name, size_t len) {
        if (len >= Size)
            len = Size - 1;

        AutoSpinLock lock(lock_);
        memcpy(name_, name, len);
        memset(name_ + len, 0, Size - len);
        return NO_ERROR;
    }

private:
    // These Names are often included for diagnostic purposes, and
    // access to the Name might be made under various other locks or
    // in interrupt context. So we use a spinlock to serialize.
    mutable SpinLock lock_;
    // This includes the trailing NUL.
    char name_[Size] TA_GUARDED(lock_) = {};
};

} // namespace mxtl
