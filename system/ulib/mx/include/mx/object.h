// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mx/object_traits.h>

namespace mx {

class port;

template <typename T> class object {
public:
    object() : value_(MX_HANDLE_INVALID) {}

    explicit object(mx_handle_t value) : value_(value) {}

    template <typename U> object(object<U>&& other) : value_(other.release()) {
        static_assert(is_same<T, void>::value, "Receiver must be compatible.");
    }

    ~object() { close(); }

    template <typename U> object<T>& operator=(object<U>&& other) {
        static_assert(is_same<T, void>::value, "Receiver must be compatible.");
        reset(other.release());
        return *this;
    }

    void reset(mx_handle_t value = MX_HANDLE_INVALID) {
        close();
        value_ = value;
    }

    void swap(object<T>& other) {
        mx_handle_t tmp = value_;
        value_ = other.value_;
        other.value_ = tmp;
    }

    mx_status_t duplicate(mx_rights_t rights, object<T>* result) const {
        static_assert(object_traits<T>::supports_duplication,
                      "Receiver must support duplication.");
        mx_handle_t h = MX_HANDLE_INVALID;
        mx_status_t status = mx_handle_duplicate(value_, rights, &h);
        result->reset(h);
        return status;
    }

    mx_status_t replace(mx_rights_t rights, object<T>* result) {
        mx_handle_t h = MX_HANDLE_INVALID;
        mx_status_t status = mx_handle_replace(value_, rights, &h);
        // We store MX_HANDLE_INVALID to value_ before calling reset on result
        // in case result == this.
        if (status == NO_ERROR)
            value_ = MX_HANDLE_INVALID;
        result->reset(h);
        return status;
    }

    mx_status_t wait_one(mx_signals_t signals, mx_time_t deadline,
                         mx_signals_t* pending) const {
        return mx_object_wait_one(value_, signals, deadline, pending);
    }

    mx_status_t wait_async(const object<port>& port, uint64_t key,
                           mx_signals_t signals, uint32_t options) const {
        return mx_object_wait_async(value_, port.get(), key, signals, options);
    }

    static mx_status_t wait_many(mx_wait_item_t* wait_items, uint32_t count, mx_time_t deadline) {
        return mx_object_wait_many(wait_items, count, deadline);
    }

    // TODO(abarth): Not all of these methods apply to every type of object. We
    // should sort out which ones apply where and limit them to the interfaces
    // where they work.

    mx_status_t signal(uint32_t clear_mask, uint32_t set_mask) const {
        static_assert(object_traits<T>::supports_user_signal,
                      "Receiver must support user signals.");
        return mx_object_signal(get(), clear_mask, set_mask);
    }

    mx_status_t signal_peer(uint32_t clear_mask, uint32_t set_mask) const {
        static_assert(object_traits<T>::supports_user_signal,
                      "Receiver must support user signals.");
        static_assert(object_traits<T>::has_peer_handle,
                      "Receiver must have peer object.");
        return mx_object_signal_peer(get(), clear_mask, set_mask);
    }

    mx_status_t get_info(uint32_t topic, void* buffer,
                         size_t buffer_size,
                         size_t* actual_count, size_t* avail_count) const {
        return mx_object_get_info(get(), topic, buffer, buffer_size, actual_count, avail_count);
    }

    mx_status_t get_child(uint64_t koid, mx_rights_t rights,
                          object<T>* result) const {
        mx_handle_t h = MX_HANDLE_INVALID;
        mx_status_t status = mx_object_get_child(value_, koid, rights, &h);
        result->reset(h);
        return status;
    }

    mx_status_t get_property(uint32_t property, void* value,
                             size_t size) const {
        return mx_object_get_property(get(), property, value, size);
    }

    mx_status_t set_property(uint32_t property, const void* value,
                             size_t size) const {
        return mx_object_set_property(get(), property, value, size);
    }

    mx_status_t get_cookie(mx_handle_t scope, uint64_t *cookie) const {
        return mx_object_get_cookie(get(), scope, cookie);
    }

    mx_status_t set_cookie(mx_handle_t scope, uint64_t cookie) {
        return mx_object_set_cookie(get(), scope, cookie);
    }

    bool is_valid() const { return value_ != MX_HANDLE_INVALID; }
    explicit operator bool() const { return is_valid(); }

    mx_handle_t get() const { return value_; }

    // Get the address of the underling internal handle storage.
    //
    // Note: The intended purpose is to facilitate interactions with C APIs
    // which expect to be provided a pointer to a handle used as an out
    // parameter.  Because of this, the expectation is that the C-API is going
    // to overwrite the contents of value_, and if value_ was a valid handle,
    // the handle would have been leaked.
    //
    // BE CAREFUL!  Misuse of this method on mx::object<> instances whose
    // current handle is valid can result in leaking handles.
    mx_handle_t* get_address() { return &value_; }

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

    object(const object<T>&) = delete;

    void operator=(const object<T>&) = delete;

    void close() {
        if (value_ != MX_HANDLE_INVALID) {
            mx_handle_close(value_);
            value_ = MX_HANDLE_INVALID;
        }
    }

    mx_handle_t value_;
};

template <typename T> bool operator==(const object<T>& a, const object<T>& b) {
    return a.get() == b.get();
}

template <typename T> bool operator!=(const object<T>& a, const object<T>& b) {
    return !(a == b);
}

template <typename T> bool operator==(mx_handle_t a, const object<T>& b) {
    return a == b.get();
}

template <typename T> bool operator!=(mx_handle_t a, const object<T>& b) {
    return !(a == b);
}

template <typename T> bool operator==(const object<T>& a, mx_handle_t b) {
    return a.get() == b;
}

template <typename T> bool operator!=(const object<T>& a, mx_handle_t b) {
    return !(a == b);
}

} // namespace mx
