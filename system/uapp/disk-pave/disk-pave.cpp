// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pave-logging.h"
#include "pave-lib.h"

namespace {

using paver::Arch;
using paver::Command;
using paver::Flags;

void PrintUsage() {
    ERROR("install-disk-image <command> [options...]\n");
    ERROR("Commands:\n");
    ERROR("  install-bootloader : Install a BOOTLOADER partition to the device\n");
    ERROR("  install-efi        : Install an EFI partition to the device\n");
    ERROR("  install-kernc      : Install a KERN-C CrOS partition to the device\n");
    ERROR("  install-zircona    : Install a ZIRCON-A partition to the device\n");
    ERROR("  install-zirconb    : Install a ZIRCON-B partition to the device\n");
    ERROR("  install-zirconr    : Install a ZIRCON-R partition to the device\n");
    ERROR("  install-fvm        : Install a sparse FVM to the device\n");
    ERROR("  wipe               : Clean up the install disk\n");
    ERROR("Options:\n");
    ERROR("  --file <file>: Read from FILE instead of stdin\n");
    ERROR("  --force: Install partition even if inappropriate for the device\n");
}

bool ParseFlags(int argc, char** argv, Flags* flags) {
#define SHIFT_ARGS \
    do {           \
        argc--;    \
        argv++;    \
    } while (0)

    // Parse command.
    if (argc < 2) {
        ERROR("install-disk-image needs a command\n");
        return false;
    }
    SHIFT_ARGS;

    if (!strcmp(argv[0], "install-bootloader")) {
        flags->cmd = Command::kInstallBootloader;
    } else if (!strcmp(argv[0], "install-efi")) {
        flags->cmd = Command::kInstallEfi;
    } else if (!strcmp(argv[0], "install-kernc")) {
        flags->cmd = Command::kInstallKernc;
    } else if (!strcmp(argv[0], "install-zircona")) {
        flags->cmd = Command::kInstallZirconA;
    } else if (!strcmp(argv[0], "install-zirconb")) {
        flags->cmd = Command::kInstallZirconB;
    } else if (!strcmp(argv[0], "install-zirconr")) {
        flags->cmd = Command::kInstallZirconR;
    } else if (!strcmp(argv[0], "install-fvm")) {
        flags->cmd = Command::kInstallFvm;
    } else if (!strcmp(argv[0], "wipe")) {
        flags->cmd = Command::kWipe;
    } else {
        ERROR("Invalid command: %s\n", argv[0]);
        return false;
    }
    SHIFT_ARGS;

    // Parse options.
    flags->force = false;
#if defined(__x86_64__)
    flags->arch = Arch::X64;
#elif defined(__aarch64__)
    flags->arch = Arch::ARM64;
#endif
    flags->payload_fd.reset(STDIN_FILENO);
    while (argc > 0) {
        if (!strcmp(argv[0], "--file")) {
            SHIFT_ARGS;
            if (argc < 1) {
                ERROR("'--file' argument requires a file\n");
                return false;
            }
            flags->payload_fd.reset(open(argv[0], O_RDONLY));
            if (!flags->payload_fd) {
                ERROR("Couldn't open supplied file\n");
                return false;
            }
        } else if (!strcmp(argv[0], "--force")) {
            flags->force = true;
        } else {
            return false;
        }
        SHIFT_ARGS;
    }
    return true;
#undef SHIFT_ARGS
}

} // namespace

int main(int argc, char** argv) {
    Flags flags;
    if (!(ParseFlags(argc, argv, &flags))) {
        PrintUsage();
        return -1;
    }
    return paver::RealMain(fbl::move(flags)) == ZX_OK ? 0 : -1;
}
