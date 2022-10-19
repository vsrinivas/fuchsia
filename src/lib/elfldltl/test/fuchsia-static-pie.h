// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_TEST_FUCHSIA_STATIC_PIE_H_
#define SRC_LIB_ELFLDLTL_TEST_FUCHSIA_STATIC_PIE_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// This is passed the vDSO base address as received in the second argument
// register from zx_process_start.  This does the initial bootstrap setup
// so that system calls are available.
void StaticPieSetup(const void* vdso_base);

// This can be called once the VMAR created by the program loader has been
// retrieved via the bootstrap protocol.  This uses that VMAR to apply any
// necessary RELRO protections.  The handle is not consumed.
zx_status_t StaticPieRelro(zx_handle_t loaded_vmar);

__END_CDECLS

#endif  // SRC_LIB_ELFLDLTL_TEST_FUCHSIA_STATIC_PIE_H_
