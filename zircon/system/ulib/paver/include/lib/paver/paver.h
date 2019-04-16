// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <zircon/types.h>

namespace paver {

// List of commands supported by paver utility.
enum class Command {
    kUnknown,
    kInstallBootloader,
    kInstallZirconA,
    kInstallZirconB,
    kInstallZirconR,
    kInstallVbMetaA,
    kInstallVbMetaB,
    kInstallVbMetaR,
    kInstallDataFile,
    kInstallFvm,
    kWipeFvm,
};

struct Flags {
    Command cmd = Command::kUnknown;
    bool force = false;
    fbl::unique_fd payload_fd;
    char* path = nullptr;
};

// Implements tool commands.
extern zx_status_t RealMain(Flags flags);

} // namespace paver
