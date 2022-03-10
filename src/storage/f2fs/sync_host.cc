// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "f2fs.h"

namespace f2fs {

zx_status_t sync_completion_wait(sync_completion_t *completion, zx_duration_t timeout) {
  if (completion) {
    return completion->wait(timeout);
  }
  return ZX_OK;
}

void sync_completion_signal(sync_completion_t *completion) {
  if (completion) {
    completion->signal();
  }
}

}  // namespace f2fs
