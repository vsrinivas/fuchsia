// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <vm/vm_object.h>
#include <fbl/ref_ptr.h>

namespace crypto {

namespace entropy {

#if ENABLE_ENTROPY_COLLECTOR_TEST

// These fields are read in kernel/lib/userboot/userboot.cpp, in order to pass
// the VmObject on to devmgr (where it's added to the filesystem).
extern fbl::RefPtr<VmObject> entropy_vmo;
extern bool entropy_was_lost;
#endif

// This function is always defined. If ENABLE_ENTROPY_COLLECTOR_TEST is set,
// this function runs the early boot test. Otherwise, it does nothing.
void EarlyBootTest();

} // namespace entropy

} // namespace crypto
