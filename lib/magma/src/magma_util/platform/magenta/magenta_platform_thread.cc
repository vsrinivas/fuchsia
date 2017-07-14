// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_thread.h"
#include <pthread.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/threads.h>

namespace magma {

uint32_t PlatformThreadId::GetCurrentThreadId() { return pthread_self(); }

void PlatformThreadHelper::SetCurrentThreadName(const std::string& name)
{
    mx_object_set_property(thrd_get_mx_handle(thrd_current()), MX_PROP_NAME, name.c_str(),
                           name.size());
}

} // namespace
