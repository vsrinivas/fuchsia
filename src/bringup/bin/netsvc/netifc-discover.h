// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#ifndef SRC_BRINGUP_BIN_NETSVC_NETIFC_DISCOVER_H_
#define SRC_BRINGUP_BIN_NETSVC_NETIFC_DISCOVER_H_

__BEGIN_CDECLS
zx_status_t netifc_discover(const char* ethdir, const char* topological_path,
                            zx_handle_t* interface, uint8_t netmac[6]);
__END_CDECLS

#endif  // SRC_BRINGUP_BIN_NETSVC_NETIFC_DISCOVER_H_
