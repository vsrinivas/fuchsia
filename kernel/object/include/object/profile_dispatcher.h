// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>
#include <zircon/syscalls/profile.h>

#include <fbl/canary.h>
#include <object/dispatcher.h>

class ProfileDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(const zx_profile_info_t& info,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    ~ProfileDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PROFILE; }
    bool has_state_tracker() const final { return false; }

private:
    explicit ProfileDispatcher(const zx_profile_info_t& info);

    fbl::Canary<fbl::magic("PROF")> canary_;
    const zx_profile_info_t info_;
};
