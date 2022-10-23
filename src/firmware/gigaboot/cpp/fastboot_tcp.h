// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_TCP_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_TCP_H_

#include <lib/zx/result.h>

namespace gigaboot {

zx::result<> FastbootTcpMain();

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_TCP_H_
