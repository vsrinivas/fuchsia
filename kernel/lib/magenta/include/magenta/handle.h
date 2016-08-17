// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/types.h>
#include <magenta/syscalls-types.h>
#include <utils/intrusive_double_list.h>
#include <utils/ref_ptr.h>

class Dispatcher;

class Handle final : public utils::DoublyLinkedListable<Handle*> {
public:
    Handle(utils::RefPtr<Dispatcher> dispatcher, mx_rights_t rights);
    Handle(const Handle* rhs, mx_rights_t rights);

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle &) = delete;

    ~Handle();

    utils::RefPtr<Dispatcher> dispatcher() const { return dispatcher_; }

    mx_koid_t process_id() const {
        return process_id_;
    }

    void set_process_id(mx_koid_t pid) {
        process_id_ = pid;
    }

    uint32_t rights() const {
        return rights_;
    }

private:
    mx_koid_t process_id_;
    utils::RefPtr<Dispatcher> dispatcher_;
    const mx_rights_t rights_;
};
