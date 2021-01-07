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

// This struct stores state of a VMO pinned to a BTI. |size| and |offset| are
// the actual size and offset used to pin pages when calling |zx_bti_pin()|;
// |vmo| is a duplicate of the original pinned VMO.
typedef struct {
  zx_handle_t vmo;
  uint64_t size;
  uint64_t offset;
} fake_bti_pinned_vmo_info_t;

// Fake BTI stores all pinned VMOs for testing purposes. Tests can call this
// method to get duplicates of all pinned VMO handles, as well as the pinned
// pages' size and offset for each VMO.
//
// |out_vmo_info| points to a buffer containing |out_num_vmos| vmo info
// elements. The method writes no more than |out_num_vmos| elements to the
// buffer, and will write the actual number of pinned vmos to |actual_num_vmos|
// if the argument is not null.
//
// It's the caller's repsonsibility to close all the returned VMO handles.
zx_status_t fake_bti_get_pinned_vmos(zx_handle_t bti, fake_bti_pinned_vmo_info_t* out_vmo_info,
                                     size_t out_num_vmos, size_t* actual_num_vmos);

__END_CDECLS

#endif  // SRC_DEVICES_TESTING_FAKE_BTI_INCLUDE_LIB_FAKE_BTI_BTI_H_
