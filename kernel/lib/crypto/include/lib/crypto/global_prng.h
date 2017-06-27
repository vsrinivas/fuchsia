// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/crypto/prng.h>

#if ENABLE_ENTROPY_COLLECTOR_TEST
#include <kernel/vm/vm_object.h>
#include <mxtl/ref_ptr.h>
#endif

namespace crypto {

namespace GlobalPRNG {

// Returns a pointer to the global PRNG singleton.  The pointer is
// guaranteed to be non-null.
PRNG* GetInstance();

#if ENABLE_ENTROPY_COLLECTOR_TEST
namespace internal {

extern mxtl::RefPtr<VmObject> entropy_vmo;
extern bool entropy_was_lost;

} // namespace internal
#endif

} //namespace GlobalPRNG

} // namespace crypto
