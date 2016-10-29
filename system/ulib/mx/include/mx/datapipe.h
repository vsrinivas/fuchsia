// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {
class datapipe_producer;
class datapipe_consumer;

template <typename T = void> class datapipe : public handle<T> {
public:
    datapipe() = default;

    explicit datapipe(mx_handle_t h) : handle<T>(h) {}

    explicit datapipe(handle<void>&& h) : handle<T>(h.release()) {}

    datapipe(datapipe&& other) : handle<T>(other.release()) {}

    static mx_status_t create(mx_size_t element_size, mx_size_t capacity,
                              uint32_t options, datapipe_producer* producer,
                              datapipe_consumer* consumer) const {
        mx_handle_t consumer_handle = MX_HANDLE_INVALID;
        mx_handle_t h = mx_datapipe_create(options, mx_size_t element_size,
                                           capacity, &consumer_handle);
        consumer->reset(consumer_handle);
        if (h < 0) {
            producer->reset(MX_HANDLE_INVALID);
            return h;
        } else {
            producer->reset(h);
            return NO_ERROR;
        }
    }
};

class datapipe_producer : public datapipe<datapipe_producer> {
public:
    datapipe_producer() = default;

    explicit datapipe_producer(mx_handle_t value) : datapipe(value) {}

    explicit datapipe_producer(handle<void>&& h) : datapipe(h.release()) {}

    datapipe_producer(datapipe_producer&& other) : datapipe(other.release()) {}

    datapipe_producer& operator=(datapipe_producer&& other) {
        reset(other.release());
        return *this;
    }

    mx_status_t write(uint32_t flags, const void* buffer, mx_size_t len,
                      mx_size_t* actual) const {
        mx_ssize_t result = mx_datapipe_write(get(), flags, len, buffer);
        if (result < 0) {
            return static_cast<mx_status_t>(result);
        } else {
            if (actual)
                *actual = result;
            return NO_ERROR;
        }
    }

    mx_status_t begin_write(uint32_t flags, uintptr_t* buffer,
                            mx_size_t* available) const {
        mx_ssize_t result = mx_datapipe_begin_write(get(), flags, buffer);
        if (result < 0) {
            return static_cast<mx_status_t>(result);
        } else {
            *available = result;
            return NO_ERROR;
        }
    }

    mx_status_t end_write(mx_size_t written) const {
        return mx_datapipe_end_write(get(), written);
    }
};

class datapipe_consumer : public datapipe<datapipe_consumer> {
public:
    datapipe_consumer() = default;

    explicit datapipe_consumer(mx_handle_t value) : datapipe(value) {}

    explicit datapipe_consumer(handle<void>&& h) : datapipe(h.release()) {}

    datapipe_consumer(datapipe_consumer&& other) : datapipe(other.release()) {}

    datapipe_consumer& operator=(datapipe_consumer&& other) {
        reset(other.release());
        return *this;
    }

    mx_status_t read(uint32_t flags, void* buffer, mx_size_t len,
                     mx_size_t* actual) const {
        mx_ssize_t result = mx_datapipe_read(get(), flags, requested, buffer);
        if (result < 0) {
            return static_cast<mx_status_t>(result);
        } else {
            *actual = result;
            return NO_ERROR;
        }
    }

    mx_status_t begin_read(uint32_t flags, uintptr_t* buffer,
                           mx_size_t* available) const {
        mx_ssize_t result = mx_datapipe_begin_read(get(), flags, buffer);
        if (result < 0) {
            return static_cast<mx_status_t>(result);
        } else {
            *available = result;
            return NO_ERROR;
        }
    }

    mx_status_t end_read(mx_size_t read) const {
        return mx_datapipe_end_read(get(), read);
    }
};

} // namespace mx
