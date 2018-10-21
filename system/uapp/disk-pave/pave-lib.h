// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include "device-partitioner.h"

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
    kInstallFvm,
    kWipe,
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
};

// Paves an image onto the disk.
extern zx_status_t PartitionPave(fbl::unique_ptr<DevicePartitioner> partitioner,
                                 fbl::unique_fd payload_fd, Partition partition_type, Arch arch);

// Reads the entire file from supplied file descriptor. This is necessary due to
// implementation of streaming protocol which forces entire file to be
// transfered.
extern void Drain(fbl::unique_fd fd);

// Implements tool commands.
extern zx_status_t RealMain(Flags flags);

} // namespace paver
