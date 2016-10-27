// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mx/handle_traits.h>

namespace mx {

template <typename T> class handle {
public:
    handle() : value_(MX_HANDLE_INVALID) {}

    explicit handle(mx_handle_t value) : value_(value) {}

    template <typename U> handle(handle<U>&& other) : value_(other.release()) {
        static_assert(is_same<T, void>::value, "Receiver must be compatible.");
    }

    ~handle() { close(); }

    template <typename U> handle<T>& operator=(handle<U>&& other) {
        static_assert(is_same<T, void>::value, "Receiver must be compatible.");
        reset(other.release());
        return *this;
    }

    void reset(mx_handle_t value = MX_HANDLE_INVALID) {
        close();
        value_ = value;
    }

    void swap(handle<T>& other) {
        mx_handle_t tmp = value_;
        value_ = other.value_;
        other.value_ = tmp;
    }

    mx_status_t duplicate(mx_rights_t rights, handle<T>* result) const {
        static_assert(handle_traits<T>::supports_duplication,
                      "Receiver must support duplication.");
        mx_handle_t h = mx_handle_duplicate(value_, rights);
        if (h < 0) {
            result->reset(MX_HANDLE_INVALID);
            return h;
        } else {
            result->reset(h);
            return NO_ERROR;
        }
    }

    mx_status_t wait_one(mx_signals_t signals, mx_time_t timeout,
                         mx_signals_state_t* signals_state) const {
        return mx_handle_wait_one(value_, signals, timeout, signals_state);
    }

    mx_status_t replace(mx_rights_t rights, handle<T>* result) const {
        mx_handle_t h = mx_handle_replace(value_, rights);
        if (h < 0) {
            result->reset(MX_HANDLE_INVALID);
            return h;
        } else {
            result->reset(h);
            return NO_ERROR;
        }
    }

    // TODO(abarth): Not all of these methods apply to every type of handle. We
    // should sort out which ones apply where and limit them to the interfaces
    // where they work.

    mx_status_t signal(uint32_t clear_mask, uint32_t set_mask) const {
        return mx_object_signal(get(), clear_mask, set_mask);
    }

    mx_status_t get_info(uint32_t topic, uint16_t topic_size, void* buffer,
                        mx_size_t buffer_size, mx_size_t* return_size) const {
        mx_ssize_t result = mx_object_get_info(get(), topic, topic_size, buffer, buffer_size);
        if (result < 0) {
            return static_cast<mx_status_t>(result);
        } else {
            *return_size = result;
            return NO_ERROR;
        }
    }

    mx_status_t get_child(uint64_t koid, mx_rights_t rights, handle<T>* result) const {
        mx_handle_t h = mx_object_get_child(value_, koid, rights);
        if (h < 0) {
            result->reset(MX_HANDLE_INVALID);
            return h;
        } else {
            result->reset(h);
            return NO_ERROR;
        }
    }
    // TODO(abarth): mx_object_bind_exception_port

    mx_status_t get_property(uint32_t property, void* value,
                             mx_size_t size) const {
        return get_property(get(), property, value, size);
    }

    mx_status_t set_property(uint32_t property, const void* value,
                             mx_size_t size) const {
        return set_property(get(), property, value, size);
    }

    explicit operator bool() const { return value_ != MX_HANDLE_INVALID; }

    mx_handle_t get() const { return value_; }

    __attribute__((warn_unused_result)) mx_handle_t release() {
        mx_handle_t result = value_;
        value_ = MX_HANDLE_INVALID;
        return result;
    }

private:
    template <typename A, typename B> struct is_same {
        static const bool value = false;
    };

    template <typename A> struct is_same<A, A> {
        static const bool value = true;
    };

    handle(const handle<T>&) = delete;

    void operator=(const handle<T>&) = delete;

    void close() {
        if (value_ != MX_HANDLE_INVALID) {
            mx_handle_close(value_);
            value_ = MX_HANDLE_INVALID;
        }
    }

    mx_handle_t value_;
};

} // namespace mx
