// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/hypervisor.h>
#include <magenta/dispatcher.h>
#include <magenta/types.h>

class HypervisorDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(mxtl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~HypervisorDispatcher();

    mx_obj_type_t get_type() const { return MX_OBJ_TYPE_HYPERVISOR; }

private:
    mxtl::unique_ptr<HypervisorContext> context_;

    explicit HypervisorDispatcher(mxtl::unique_ptr<HypervisorContext> context);
};
