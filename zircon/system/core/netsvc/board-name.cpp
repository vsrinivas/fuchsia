// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "board-name.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <gpt/gpt.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/fdio.h>
#include <zircon/status.h>

#include <algorithm>

namespace {

fbl::unique_fd FindGpt() {
    constexpr char kBlockDevPath[] = "/dev/class/block/";
    fbl::unique_fd d_fd(open(kBlockDevPath, O_RDONLY));
    if (!d_fd) {
        fprintf(stderr, "netsvc: Cannot inspect block devices\n");
        return fbl::unique_fd();
    }
    DIR* d = fdopendir(d_fd.release());
    if (d == nullptr) {
        fprintf(stderr, "netsvc: Cannot inspect block devices\n");
        return fbl::unique_fd();
    }
    const auto closer = fbl::MakeAutoCall([&]() { closedir(d); });

    char path[PATH_MAX] = {};

    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        fbl::unique_fd fd(openat(dirfd(d), de->d_name, O_RDWR));
        if (!fd) {
            continue;
        }

        zx::channel dev;
        zx_status_t status = fdio_get_service_handle(fd.release(), dev.reset_and_get_address());

        fuchsia_hardware_block_BlockInfo info;

        zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(dev.get(),
                                                                    &status, &info);
        if (io_status != ZX_OK || status != ZX_OK) {
            continue;
        }
        size_t path_len;
        path[PATH_MAX - 1] = '\0';
        io_status = fuchsia_device_ControllerGetTopologicalPath(dev.get(), &status, path,
                                                                PATH_MAX - 1, &path_len);
        if (io_status != ZX_OK || status != ZX_OK) {
            continue;
        }
        path[path_len] = 0;

        // TODO(ZX-1344): This is a hack, but practically, will work for our
        // usage.
        //
        // The GPT which will contain an FVM should be the first non-removable
        // block device that isn't a partition itself.
        if (!(info.flags & BLOCK_FLAG_REMOVABLE) && strstr(path, "part-") == nullptr) {
            return fbl::unique_fd(open(path, O_RDWR));
        }
    }

    return fbl::unique_fd();
}

static bool IsChromebook() {
    fbl::unique_fd gpt_fd(FindGpt());
    if (!gpt_fd) {
        return false;
    }
    fzl::UnownedFdioCaller caller(gpt_fd.get());
    fuchsia_hardware_block_BlockInfo block_info;
    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &status,
                                                                &block_info);
    if (io_status != ZX_OK) {
        status = io_status;
    }
    if (status != ZX_OK) {
        fprintf(stderr, "netsvc: Could not acquire GPT block info: %s\n",
                zx_status_get_string(status));
        return false;
    }
    fbl::unique_ptr<gpt::GptDevice> gpt;
    status = gpt::GptDevice::Create(gpt_fd.get(), block_info.block_size, block_info.block_count,
                                    &gpt);
    if (status != ZX_OK) {
        fprintf(stderr, "netsvc: Failed to get GPT info: %s\n", zx_status_get_string(status));
        return false;
    }
    return is_cros(gpt.get());
}

} // namespace

bool CheckBoardName(const zx::channel& sysinfo, const char* name, size_t length) {
    if (!sysinfo) {
        return false;
    }

    length = std::min(length, ZX_MAX_NAME_LEN);

    char real_board_name[ZX_MAX_NAME_LEN] = {};
    zx_status_t status;
    size_t actual_size;
    zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetBoardName(
        sysinfo.get(), &status, real_board_name, sizeof(real_board_name), &actual_size);
    if (fidl_status != ZX_OK || status != ZX_OK) {
        return false;
    }

    // Special case x64 to check if chromebook.
    if (!strcmp(real_board_name, "pc") && IsChromebook()) {
        strcpy(real_board_name, "chromebook-x64");
    }

    return strncmp(real_board_name, name, length) == 0;
}
