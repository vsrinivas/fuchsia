// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <object/handles.h>

class Handle;

// HandleOwner wraps a Handle in a unique_ptr-like object that has single ownership of the
// Handle and deletes it (via DeleteHandle) whenever it falls out of scope.
class HandleOwner {
public:
    HandleOwner() = default;
    HandleOwner(decltype(nullptr)) : h_(nullptr) {}

    explicit HandleOwner(Handle* h) : h_(h) {}

    HandleOwner(const HandleOwner&) = delete;
    HandleOwner& operator=(const HandleOwner&) = delete;

    HandleOwner(HandleOwner&& other) : h_(other.release()) {}

    HandleOwner& operator=(HandleOwner&& other) {
        reset(other.release());
        return *this;
    }

    ~HandleOwner() {
        if (h_) DeleteHandle(h_);
    }

    Handle* operator->() {
        return h_;
    }

    Handle* get() const {
        return h_;
    }

    Handle* release() {
        Handle* h = h_;
        h_ = nullptr;
        return h;
    }

    void reset(Handle* h) {
        if (h_) DeleteHandle(h_);
        h_ = h;
    }

    void swap(HandleOwner& other) {
        Handle* h = h_;
        h_ = other.h_;
        other.h_ = h;
    }

    explicit operator bool() { return h_ != nullptr; }

private:
    Handle* h_ = nullptr;
};
