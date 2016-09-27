// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>

#include <sys/types.h>

class EventDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t options, mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~EventDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_EVENT; }

    StateTracker* get_state_tracker() final { return &state_tracker_; }

    status_t UserSignal(uint32_t clear_mask, uint32_t set_mask) final;

private:
    explicit EventDispatcher(uint32_t options);
    NonIrqStateTracker state_tracker_;
};
