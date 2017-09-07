// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/debuglog.h>
#include <object/dispatcher.h>
#include <object/state_tracker.h>

#include <magenta/types.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>

class LogDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(uint32_t flags, fbl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~LogDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_LOG; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }

    mx_status_t Write(uint32_t flags, const void* ptr, size_t len);
    mx_status_t Read(uint32_t flags, void* ptr, size_t len, size_t* actual);

private:
    explicit LogDispatcher(uint32_t flags);

    static void Notify(void* cookie);
    void Signal();

    fbl::Canary<fbl::magic("LOGD")> canary_;

    dlog_reader reader_;
    uint32_t flags_;

    fbl::Mutex lock_;
    StateTracker state_tracker_;
};
