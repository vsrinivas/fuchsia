// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_BOARD_INFO_H_
#define SRC_BRINGUP_BIN_NETSVC_BOARD_INFO_H_

#include <lib/zx/channel.h>
#include <unistd.h>

bool CheckBoardName(const zx::channel& sysinfo, const char* name, size_t length);

bool ReadBoardInfo(const zx::channel& sysinfo, void* data, off_t offset, size_t* length);

size_t BoardInfoSize();

#endif  // SRC_BRINGUP_BIN_NETSVC_BOARD_INFO_H_
