// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/rights.h>
#include <zircon/types.h>

#include <zircon/syscalls/profile.h>

#include <fbl/canary.h>
#include <object/dispatcher.h>
#include <object/handle.h>

class ProfileDispatcher final :
    public SoloDispatcher<ProfileDispatcher, ZX_DEFAULT_PROFILE_RIGHTS> {
public:
    static zx_status_t Create(const zx_profile_info_t& info,
                              KernelHandle<ProfileDispatcher>* handle,
                              zx_rights_t* rights);

    ~ProfileDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PROFILE; }

    zx_status_t ApplyProfile(fbl::RefPtr<ThreadDispatcher> thread);

private:
    explicit ProfileDispatcher(const zx_profile_info_t& info);

    const zx_profile_info_t info_;
};
