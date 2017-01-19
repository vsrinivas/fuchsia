// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

class Handle;

// Deletes a |handle| made by MakeHandle() or DupHandle().
void DeleteHandle(Handle* handle);

// HandleUniquePtr wraps a Handle in a unique_ptr-like object that has single ownership of the
// Handle and deletes it (via DeleteHandle) whenever it falls out of scope.
class HandleUniquePtr {
public:
    HandleUniquePtr() = default;
    HandleUniquePtr(decltype(nullptr)) : h_(nullptr) {}

    explicit HandleUniquePtr(Handle* h) : h_(h) {}

    HandleUniquePtr(const HandleUniquePtr&) = delete;
    HandleUniquePtr& operator=(const HandleUniquePtr&) = delete;

    HandleUniquePtr(HandleUniquePtr&& other) : h_(other.release()) {}

    HandleUniquePtr& operator=(HandleUniquePtr&& other) {
        reset(other.release());
        return *this;
    }

    ~HandleUniquePtr() {
        if (h_) DeleteHandle(h_);
    }

    Handle* operator->() {
        return h_;
    }

    Handle* get() {
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

    void swap(HandleUniquePtr& other) {
        Handle* h = h_;
        h_ = other.h_;
        other.h_ = h;
    }

    explicit operator bool() { return h_ != nullptr; }

private:
    Handle* h_ = nullptr;
};
