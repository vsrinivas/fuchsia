// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/intrin.h>
#include <platform.h>

#include <lib/concurrent/seqlock.inc>

namespace internal {

struct FuchsiaKernelOsal {
  static zx_time_t GetClockMonotonic() { return current_time(); }
  static void ArchYield() { arch::Yield(); }
};

}  // namespace internal

// Manually expand the SeqLock template using the Fuchsia user-mode OSAL
template class ::concurrent::internal::SeqLock<::internal::FuchsiaKernelOsal>;
