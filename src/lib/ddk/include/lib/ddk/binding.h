// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_LIB_DDK_BINDING_H_
#define SRC_LIB_DDK_INCLUDE_LIB_DDK_BINDING_H_

#include <ddk/binding_priv.h>

#define ZIRCON_DRIVER_BEGIN(Driver, Ops, VendorName, Version, BindCount) \
  ZIRCON_DRIVER_BEGIN_PRIV_V1(Driver, Ops, VendorName, Version, BindCount)

#define ZIRCON_DRIVER_END(Driver) ZIRCON_DRIVER_END_PRIV_V1(Driver);

#endif  // SRC_LIB_DDK_INCLUDE_LIB_DDK_BINDING_H_
