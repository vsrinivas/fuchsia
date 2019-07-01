// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "board-name.h"
#include "lib/fidl/cpp/message_part.h"
#include "lib/fidl/llcpp/traits.h"
#include "zircon/types.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#undef VMOID_INVALID
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
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

        uint8_t out_buffer[fidl::MaxSizeInChannel<::llcpp::fuchsia::hardware::block::Block::GetInfoResponse>()] = {};
        ::llcpp::fuchsia::hardware::block::BlockInfo* info;

        auto decoded = ::llcpp::fuchsia::hardware::block::Block::Call::GetInfo(
            zx::unowned_channel(dev.get()), fidl::BytePart::WrapEmpty(out_buffer), &status,
            &info);
        if (decoded.status != ZX_OK || status != ZX_OK) {
            continue;
        }
        uint8_t out_buffer2[fidl::MaxSizeInChannel<::llcpp::fuchsia::device::Controller::GetTopologicalPathResponse>()] = {};
        fidl::StringView path_view;
        auto decoded2 = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
            zx::unowned_channel(dev.get()), fidl::BytePart::WrapEmpty(out_buffer2), &status,
            &path_view);
        if (decoded2.status != ZX_OK || status != ZX_OK) {
            continue;
        }

        path[PATH_MAX - 1] = '\0';
        strncpy(path, path_view.data(), std::min<size_t>(PATH_MAX, path_view.size()));

        // TODO(ZX-1344): This is a hack, but practically, will work for our
        // usage.
        //
        // The GPT which will contain an FVM should be the first non-removable
        // block device that isn't a partition itself.
        if (!(info->flags & BLOCK_FLAG_REMOVABLE) && strstr(path, "part-") == nullptr) {
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
    uint8_t out_buffer[fidl::MaxSizeInChannel<::llcpp::fuchsia::hardware::block::Block::GetInfoResponse>()] = {};
    ::llcpp::fuchsia::hardware::block::BlockInfo* block_info;

    zx_status_t status;
    auto decoded = ::llcpp::fuchsia::hardware::block::Block::Call::GetInfo(
        zx::unowned_channel(caller.borrow_channel()), fidl::BytePart::WrapEmpty(out_buffer),
        &status, &block_info);
    if (decoded.status != ZX_OK) {
        status = decoded.status;
    }
    if (status != ZX_OK) {
        fprintf(stderr, "netsvc: Could not acquire GPT block info: %s\n",
                zx_status_get_string(status));
        return false;
    }
    fbl::unique_ptr<gpt::GptDevice> gpt;
    status = gpt::GptDevice::Create(gpt_fd.get(), block_info->block_size, block_info->block_count,
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

    uint8_t out_buffer[fidl::MaxSizeInChannel<::llcpp::fuchsia::sysinfo::Device::GetBoardNameResponse>()] = {};
    fidl::StringView board_name;
    zx_status_t status;
    auto decoded = ::llcpp::fuchsia::sysinfo::Device::Call::GetBoardName(
        zx::unowned_channel(sysinfo), fidl::BytePart::WrapEmpty(out_buffer), &status, &board_name);
    if (decoded.status != ZX_OK || status != ZX_OK) {
        return false;
    }

    strncpy(real_board_name, board_name.data(), std::min<size_t>(ZX_MAX_NAME_LEN, board_name.size()));

    // Special case x64 to check if chromebook.
#if __x86_64__
    if (IsChromebook()) {
        strcpy(real_board_name, "chromebook-x64");
    } else {
        strcpy(real_board_name, "pc");
    }
#endif

    return strncmp(real_board_name, name, length) == 0;
}
