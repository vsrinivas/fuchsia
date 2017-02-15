// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/hypervisor_dispatcher.h>

class GuestDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(mxtl::RefPtr<HypervisorDispatcher> hypervisor,
                              mxtl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~GuestDispatcher();

    mx_obj_type_t get_type() const { return MX_OBJ_TYPE_GUEST; }
    mx_status_t Start(uintptr_t entry, uintptr_t stack);

private:
    mxtl::RefPtr<HypervisorDispatcher> hypervisor_;
    mxtl::unique_ptr<GuestContext> context_;

    explicit GuestDispatcher(mxtl::RefPtr<HypervisorDispatcher> hypervisor,
                             mxtl::unique_ptr<GuestContext> context);
};
