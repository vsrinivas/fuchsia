// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/prctl.h>
#include <magenta/types.h>
#include <magenta/syscalls-types.h>

#include <utils/ref_ptr.h>
#include <utils/unique_ptr.h>

class Handle;
class Dispatcher;
class ExceptionPort;

// Creates a handle attached to |dispatcher| and with |rights| from a
// specific arena which makes their addresses come from a fixed range.
Handle* MakeHandle(utils::RefPtr<Dispatcher> dispatcher, mx_rights_t rights);

// Duplicate a handle created by MakeHandle().
Handle* DupHandle(Handle* source, mx_rights_t rights);

// Deletes a |handle| made by MakeHandle() or DupHandle().
void DeleteHandle(Handle* handle);

// Maps a handle created by MakeHandle() to the 0 to 2^32 range.
uint32_t MapHandleToU32(Handle* handle);

// Maps an integer obtained by MapHandleToU32() back to a Handle.
Handle* MapU32ToHandle(uint32_t value);

// Set/get the system exception port.
mx_status_t SetSystemExceptionPort(utils::RefPtr<ExceptionPort> eport);
void ResetSystemExceptionPort();
utils::RefPtr<ExceptionPort> GetSystemExceptionPort();

struct handle_delete {
    inline void operator()(Handle* h) const {
        DeleteHandle(h);
    }
};

bool magenta_rights_check(mx_rights_t actual, mx_rights_t desired);

using HandleUniquePtr = utils::unique_ptr<Handle, handle_delete>;

// (temporary) conversion from mx_time (nanoseconds) to lk_time_t (milliseconds)
// remove once mx_time_t is converted to 1:1 match mx_time_t
inline lk_time_t mx_time_to_lk(mx_time_t mxt) {
    if (mxt == MX_TIME_INFINITE)
        return INFINITE_TIME;

    uint64_t temp = mxt / 1000000; // nanosecs to milliseconds
    if (temp >= UINT32_MAX)
        return UINT32_MAX - 1;
    return static_cast<lk_time_t>(temp & 0xffffffff);
}

// TODO(vtl): This should use mx_time_t, but currently everything operates using lk_time_t.
inline lk_time_t timeout_to_deadline(lk_time_t now, lk_time_t timeout) {
    // Note: |lk_time_t| is a |uint32_t|.
    return (timeout > UINT32_MAX - now) ? INFINITE_TIME : now + timeout;
}

mx_status_t magenta_sleep(mx_time_t nanoseconds);
