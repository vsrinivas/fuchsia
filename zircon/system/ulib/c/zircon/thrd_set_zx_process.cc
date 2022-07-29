// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/threads.h>

#include <utility>

#include "threads_impl.h"

__EXPORT zx_handle_t thrd_set_zx_process(zx_handle_t proc_handle) {
  return std::exchange(__pthread_self()->process_handle, proc_handle);
}
