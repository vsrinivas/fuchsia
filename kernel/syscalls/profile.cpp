// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>

#include <lib/counters.h>
#include <lib/ktrace.h>

#include <object/handle.h>
#include <object/profile_dispatcher.h>

#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <object/resource.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/type_support.h>

#include "priv.h"

using fbl::AutoLock;

KCOUNTER(profile_create, "kernel.profile.create");
KCOUNTER(profile_set,    "kernel.profile.set");


zx_status_t sys_profile_create(zx_handle_t resource,
                               user_in_ptr<const zx_profile_info_t> user_profile_info,
                               user_out_handle* out) {
    // TODO(cpu): check job policy.

    zx_status_t status = validate_resource(resource, ZX_RSRC_KIND_ROOT);
    if (status != ZX_OK)
        return status;

    zx_profile_info_t profile_info;
    status = user_profile_info.copy_from_user(&profile_info);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    status = ProfileDispatcher::Create(profile_info, &dispatcher, &rights);
    if (status != ZX_OK)
        return status;

    return out->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_object_set_profile(zx_handle_t handle,
                                   zx_handle_t profile_handle,
                                   uint32_t options) {
    auto up = ProcessDispatcher::GetCurrent();

    // TODO(cpu): support more than thread objects, and actually do something.

    fbl::RefPtr<ThreadDispatcher> thread;
    auto status = up->GetDispatcherWithRights(handle, ZX_RIGHT_MANAGE_THREAD, &thread);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<ProfileDispatcher> profile;
    zx_status_t result =
        up->GetDispatcherWithRights(profile_handle, ZX_RIGHT_APPLY_PROFILE, &profile);
    if (result != ZX_OK)
        return result;

    return profile->ApplyProfile(fbl::move(thread));
}

