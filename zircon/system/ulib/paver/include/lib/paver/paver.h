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
    kInstallEfi,
    kInstallKernc,
    kInstallZirconA,
    kInstallZirconB,
    kInstallZirconR,
    kInstallVbMetaA,
    kInstallVbMetaB,
    kInstallDataFile,
    kInstallFvm,
    kWipeFvm,
};

// Architecture of device being paved. Used for payload validation.
enum class Arch {
    X64,
    ARM64,
};

struct Flags {
    Command cmd = Command::kUnknown;
    Arch arch = Arch::X64;
    bool force = false;
    fbl::unique_fd payload_fd;
    char* path = nullptr;
};

// Implements tool commands.
extern zx_status_t RealMain(Flags flags);

} // namespace paver
