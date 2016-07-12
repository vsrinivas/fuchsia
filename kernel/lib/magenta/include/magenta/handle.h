// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/types.h>
#include <magenta/syscalls-types.h>
#include <utils/ref_ptr.h>

class Dispatcher;

class Handle final {
public:
    Handle(utils::RefPtr<Dispatcher> dispatcher, mx_rights_t rights);
    Handle(const Handle* rhs, mx_rights_t rights);
    Handle(const Handle&) = delete;
    ~Handle();

    utils::RefPtr<Dispatcher> dispatcher();

    mx_pid_t process_id() const {
        return process_id_;
    }

    void set_process_id(mx_pid_t pid) {
        process_id_ = pid;
    }

    uint32_t rights() const {
        return rights_;
    }

    Handle* list_prev() {
        return prev_;
    }

    Handle* list_next() {
        return next_;
    }

    const Handle* list_prev() const {
        return prev_;
    }

    const Handle* list_next() const {
        return next_;
    }

    void list_set_prev(Handle* node) {
        prev_ = node;
    }

    void list_set_next(Handle* node) {
        next_ = node;
    }

private:
    mx_pid_t process_id_;
    const mx_rights_t rights_;
    utils::RefPtr<Dispatcher> dispatcher_;
    Handle* prev_;
    Handle* next_;
};
