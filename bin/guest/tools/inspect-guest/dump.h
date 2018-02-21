// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_TOOLS_INSPECT_GUEST_DUMP_H_
#define GARNET_BIN_GUEST_TOOLS_INSPECT_GUEST_DUMP_H_

#include <zircon/types.h>

void handle_dump(zx_vaddr_t addr, size_t len);

#endif  // GARNET_BIN_GUEST_TOOLS_INSPECT_GUEST_DUMP_H_
