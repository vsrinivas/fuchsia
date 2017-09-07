// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/types.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>

class Dispatcher;
class Handle;

namespace internal {
// Do not call: exposed only so Handle can declare it as a friend.
void TearDownHandle(Handle* handle);
} // namespace internal

// A Handle is how a specific process refers to a specific Dispatcher.
class Handle final : public fbl::DoublyLinkedListable<Handle*> {
public:
    // Returns the Dispatcher to which this instance points.
    fbl::RefPtr<Dispatcher> dispatcher() const;

    // Returns the process that owns this instance. Used to guarantee
    // that one process may not access a handle owned by a different process.
    mx_koid_t process_id() const {
        return process_id_;
    }

    // Sets the value returned by process_id().
    void set_process_id(mx_koid_t pid) {
        process_id_ = pid;
    }

    // Returns the |rights| parameter that was provided when this instance
    // was created.
    uint32_t rights() const {
        return rights_;
    }

    // Returns true if this handle has all of the desired rights bits set.
    bool HasRights(mx_rights_t desired) const {
        return (rights_ & desired) == desired;
    }

    // Returns a value that can be decoded by MapU32ToHandle() to derive
    // a pointer to this instance. ProcessDispatcher will XOR this with
    // its |handle_rand_| to create the mx_handle_t value that user
    // space sees.
    uint32_t base_value() const {
        return base_value_;
    }

private:
    // Handle should never be created by anything other than
    // MakeHandle or DupHandle.
    friend Handle* MakeHandle(fbl::RefPtr<Dispatcher> dispatcher,
                              mx_rights_t rights);
    friend Handle* DupHandle(Handle* source, mx_rights_t rights, bool is_replace);
    Handle(const Handle&) = delete;
    Handle(fbl::RefPtr<Dispatcher> dispatcher, mx_rights_t rights,
           uint32_t base_value);
    Handle(const Handle* rhs, mx_rights_t rights, uint32_t base_value);

    Handle& operator=(const Handle&) = delete;

    // Handle should never be destroyed by anything other than DeleteHandle,
    // which uses TearDownHandle to do the actual destruction.
    friend void internal::TearDownHandle(Handle* handle);
    ~Handle() = default;

    mx_koid_t process_id_;
    fbl::RefPtr<Dispatcher> dispatcher_;
    const mx_rights_t rights_;
    const uint32_t base_value_;
};
