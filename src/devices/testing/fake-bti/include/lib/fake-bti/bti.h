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

__END_CDECLS

#endif  // SRC_DEVICES_TESTING_FAKE_BTI_INCLUDE_LIB_FAKE_BTI_BTI_H_
