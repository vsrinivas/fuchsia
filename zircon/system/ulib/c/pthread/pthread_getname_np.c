// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _BSD_SOURCE  // For strlcpy.
#include <string.h>
#include <zircon/compiler.h>

#include "libc.h"
#include "threads_impl.h"

__EXPORT int pthread_getname_np(pthread_t thread, char *name, size_t len) {
  char namebuf[ZX_MAX_NAME_LEN];
  zx_handle_t handle = zxr_thread_get_handle(&thread->zxr_thread);
  zx_status_t status = _zx_object_get_property(handle, ZX_PROP_NAME, namebuf, ZX_MAX_NAME_LEN);
  ZX_ASSERT(status == ZX_OK);
  strlcpy(name, namebuf, len);
  return 0;
}
