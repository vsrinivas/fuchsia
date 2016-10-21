// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread.h"

#include <magenta/syscalls.h>

#include "lib/ftl/logging.h"

namespace debugserver {

Thread::Thread(Process* process, mx_handle_t debug_handle, mx_koid_t thread_id)
    : process_(process),
      debug_handle_(debug_handle),
      thread_id_(thread_id),
      registers_(arch::Registers::Create(debug_handle)),
      weak_ptr_factory_(this) {
  FTL_DCHECK(process_);
  FTL_DCHECK(debug_handle_ != MX_HANDLE_INVALID);
  FTL_DCHECK(thread_id_ != MX_KOID_INVALID);
  FTL_DCHECK(registers_.get());
}

Thread::~Thread() {
  mx_handle_close(debug_handle_);
}

ftl::WeakPtr<Thread> Thread::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

}  // namespace debugserver
