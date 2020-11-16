// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_BTI_INCLUDE_LIB_FAKE_BTI_BTI_H_
#define SRC_DEVICES_TESTING_FAKE_BTI_INCLUDE_LIB_FAKE_BTI_BTI_H_

#include <limits.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// All physical addresses returned by zx_bti_pin with a fake BTI will be set to this value.
// PAGE_SIZE is chosen so that so superficial validity checks like "is the address correctly
// aligned" and "is the address non-zero" in the code under test will pass.
#define FAKE_BTI_PHYS_ADDR PAGE_SIZE

zx_status_t fake_bti_create(zx_handle_t* out);

// Like fake_bti_create, except zx_bti_pin will return the fake physical addresses in |paddrs|, or
// ZX_ERR_OUT_OF_RANGE if not enough address were specified. If |paddrs| is NULL or paddr_count is
// zero, each address is set to FAKE_BTI_PHYS_ADDR, and no range check is performed. |paddrs| must
// remain valid until the last call to zx_bti_pin is made.
zx_status_t fake_bti_create_with_paddrs(const zx_paddr_t* paddrs, size_t paddr_count,
                                        zx_handle_t* out);

__END_CDECLS

#endif  // SRC_DEVICES_TESTING_FAKE_BTI_INCLUDE_LIB_FAKE_BTI_BTI_H_
