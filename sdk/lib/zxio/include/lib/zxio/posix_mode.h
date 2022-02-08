// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_POSIX_MODE_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_POSIX_MODE_H_

#include <lib/zxio/types.h>

__BEGIN_CDECLS

// posix mode conversions ------------------------------------------------------

// These are defined in zxio today because the "mode" field in
// |fuchsia.io/NodeAttributes| is POSIX, whereas the "protocols" and "abilities"
// field in |zxio_node_attributes_t| aligns with |fuchsia.io|.

uint32_t zxio_get_posix_mode(zxio_node_protocols_t protocols, zxio_abilities_t abilities);

__END_CDECLS

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_POSIX_MODE_H_
