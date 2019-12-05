// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/block-group-registry.h>

#include <pthread.h>

#include <fbl/auto_lock.h>
#include <zircon/assert.h>

namespace block_client {

groupid_t BlockGroupRegistry::GroupID() {
  fbl::AutoLock lock(&lock_);
  auto tid = pthread_self();
  for (groupid_t i = 0; i < threads_.size(); i++) {
    if (threads_[i]) {
      if (pthread_equal(threads_[i].value(), tid)) {
        return i;
      }
    } else {
      threads_[i] = tid;
      return i;
    }
  }
  ZX_ASSERT_MSG(false, "Too many threads accessing block device");
}

}  // namespace block_client
