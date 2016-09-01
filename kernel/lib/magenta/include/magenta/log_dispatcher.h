// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/debuglog.h>

#include <magenta/dispatcher.h>

class LogDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights);

    ~LogDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_LOG; }

    status_t Write(const void* ptr, size_t len, uint32_t flags);
    status_t Read(void* ptr, size_t len, uint32_t flags);
    status_t ReadFromUser(void* userptr, size_t len, uint32_t flags);

private:
    explicit LogDispatcher(uint32_t flags);
    dlog_reader reader_;
    uint32_t flags_;
};
