// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_NETSVC_H_
#define SRC_BRINGUP_BIN_NETSVC_NETSVC_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

bool netbootloader();
const char* nodename();
bool all_features();

__END_CDECLS

#endif  // SRC_BRINGUP_BIN_NETSVC_NETSVC_H_
