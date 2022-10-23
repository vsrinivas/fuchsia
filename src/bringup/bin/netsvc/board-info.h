// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_BOARD_INFO_H_
#define SRC_BRINGUP_BIN_NETSVC_BOARD_INFO_H_

#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <lib/zx/result.h>
#include <unistd.h>

zx::result<bool> CheckBoardName(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo,
                                const char* name, size_t length);

zx::result<> ReadBoardInfo(fidl::UnownedClientEnd<fuchsia_sysinfo::SysInfo> sysinfo, void* data,
                           off_t offset, size_t* length);

size_t BoardInfoSize();

#endif  // SRC_BRINGUP_BIN_NETSVC_BOARD_INFO_H_
