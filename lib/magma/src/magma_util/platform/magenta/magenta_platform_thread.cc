// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_thread.h"
#include <pthread.h>

namespace magma {

uint32_t PlatformThreadId::GetCurrentThreadId() { return pthread_self(); }

} // namespace