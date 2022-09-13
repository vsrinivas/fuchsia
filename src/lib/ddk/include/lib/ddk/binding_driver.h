// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_LIB_DDK_BINDING_DRIVER_H_
#define SRC_LIB_DDK_INCLUDE_LIB_DDK_BINDING_DRIVER_H_

#include <lib/ddk/binding_priv.h>

#define ZIRCON_DRIVER(Driver, Ops, VendorName, Version)            \
  ZIRCON_DRIVER_BEGIN_PRIV_V2(Driver, Ops, VendorName, Version, 1) \
  0x0, ZIRCON_DRIVER_END_PRIV_V2(Driver)

#endif  // SRC_LIB_DDK_INCLUDE_LIB_DDK_BINDING_DRIVER_H_
