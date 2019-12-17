// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_FD_H_
#define SRC_LEDGER_BIN_PLATFORM_FD_H_

#include <lib/zx/channel.h>

#include "src/ledger/lib/files/unique_fd.h"

namespace ledger {

zx::channel CloneChannelFromFileDescriptor(int fd);

unique_fd OpenChannelAsFileDescriptor(zx::channel channel);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_FD_H_
