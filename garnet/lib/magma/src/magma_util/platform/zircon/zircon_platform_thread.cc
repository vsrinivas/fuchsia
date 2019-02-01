// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_object.h"
#include "platform_thread.h"

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/threads.h>

namespace magma {

uint64_t PlatformThreadId::GetCurrentThreadId()
{
    uint64_t koid;
    PlatformObject::IdFromHandle(thrd_get_zx_handle(thrd_current()), &koid);
    return koid;
}

void PlatformThreadHelper::SetCurrentThreadName(const std::string& name)
{
    zx_object_set_property(thrd_get_zx_handle(thrd_current()), ZX_PROP_NAME, name.c_str(),
                           name.size());
}

std::string PlatformThreadHelper::GetCurrentThreadName()
{
    char name[ZX_MAX_NAME_LEN];
    zx_status_t status = zx_object_get_property(thrd_get_zx_handle(thrd_current()), ZX_PROP_NAME,
                                                name, sizeof(name));
    return (status == ZX_OK) ? std::string(name) : std::string();
}

std::string PlatformProcessHelper::GetCurrentProcessName()
{
    char name[ZX_MAX_NAME_LEN];
    zx_status_t status =
        zx_object_get_property(zx_process_self(), ZX_PROP_NAME, name, sizeof(name));
    return (status == ZX_OK) ? std::string(name) : std::string();
}

} // namespace magma
