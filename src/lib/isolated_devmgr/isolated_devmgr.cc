// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "isolated_devmgr.h"

#include <fcntl.h>
#include <lib/async/default.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/unsafe.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>
#include <zircon/syscalls/debug.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <sdk/lib/sys/cpp/component_context.h>

namespace isolated_devmgr {

zx_status_t IsolatedDevmgr::WaitForFile(const char* path) {
  fbl::unique_fd out;
  return device_watcher::RecursiveWaitForFile(devmgr_.devfs_root(), path, &out);
}

void IsolatedDevmgr::Connect(zx::channel req) {
  fdio_cpp::UnownedFdioCaller fd(devmgr_.devfs_root().get());
  fdio_service_clone_to(fd.borrow_channel(), req.release());
}

namespace {

// This board driver knows how to interpret the metadata for which devices to
// spawn.
const zbi_platform_id_t kPlatformId = []() {
  zbi_platform_id_t plat_id = {};
  plat_id.vid = PDEV_VID_TEST;
  plat_id.pid = PDEV_PID_PBUS_TEST;
  strcpy(plat_id.board_name, "driver-integration-test");
  return plat_id;
}();

#define BOARD_REVISION_TEST 42

const zbi_board_info_t kBoardInfo = []() {
  zbi_board_info_t board_info = {};
  board_info.revision = BOARD_REVISION_TEST;
  return board_info;
}();

// This function is responsible for serializing driver data. It must be kept
// updated with the function that deserialized the data. This function
// is TestBoard::FetchAndDeserialize.
zx_status_t GetBootItem(const fbl::Vector<board_test::DeviceEntry>& entries, uint32_t type,
                        uint32_t extra, zx::vmo* out, uint32_t* length) {
  zx::vmo vmo;
  switch (type) {
    case ZBI_TYPE_PLATFORM_ID: {
      zx_status_t status = zx::vmo::create(sizeof(kPlatformId), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&kPlatformId, 0, sizeof(kPlatformId));
      if (status != ZX_OK) {
        return status;
      }
      *length = sizeof(kPlatformId);
      break;
    }
    case ZBI_TYPE_DRV_BOARD_INFO: {
      zx_status_t status = zx::vmo::create(sizeof(kBoardInfo), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&kBoardInfo, 0, sizeof(kBoardInfo));
      if (status != ZX_OK) {
        return status;
      }
      *length = sizeof(kBoardInfo);
      break;
    }
    case ZBI_TYPE_DRV_BOARD_PRIVATE: {
      size_t list_size = sizeof(board_test::DeviceList);
      size_t entry_size = entries.size() * sizeof(board_test::DeviceEntry);

      size_t metadata_size = 0;
      for (board_test::DeviceEntry& entry : entries) {
        metadata_size += entry.metadata_size;
      }

      zx_status_t status = zx::vmo::create(list_size + entry_size + metadata_size, 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }

      // Write DeviceList to vmo.
      board_test::DeviceList list{.count = entries.size()};
      status = vmo.write(&list, 0, sizeof(list));
      if (status != ZX_OK) {
        return status;
      }

      // Write DeviceEntries to vmo.
      status = vmo.write(entries.data(), list_size, entry_size);
      if (status != ZX_OK) {
        return status;
      }

      // Write Metadata to vmo.
      size_t write_offset = list_size + entry_size;
      for (board_test::DeviceEntry& entry : entries) {
        status = vmo.write(entry.metadata, write_offset, entry.metadata_size);
        if (status != ZX_OK) {
          return status;
        }
        write_offset += entry.metadata_size;
      }

      *length = static_cast<uint32_t>(list_size + entry_size + metadata_size);
      break;
    }
    default:
      break;
  }
  *out = std::move(vmo);
  return ZX_OK;
}

}  // namespace

std::unique_ptr<IsolatedDevmgr> IsolatedDevmgr::Create(
    devmgr_launcher::Args args,
    std::unique_ptr<fbl::Vector<board_test::DeviceEntry>> device_list_unique_ptr,
    async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }

  devmgr_integration_test::IsolatedDevmgr devmgr;
  if (device_list_unique_ptr != nullptr) {
    args.get_boot_item = [dev_list_ptr = std::move(device_list_unique_ptr)](
                             uint32_t type, uint32_t extra, zx::vmo* out, uint32_t* length) {
      return GetBootItem(*dev_list_ptr, type, extra, out, length);
    };
  }

  zx_status_t status =
      devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), dispatcher, &devmgr);

  if (status == ZX_OK) {
    return std::make_unique<IsolatedDevmgr>(std::move(devmgr));
  } else {
    FX_LOGS(ERROR) << "Failed to create devmgr: " << zx_status_get_string(status);
    return nullptr;
  }
}

}  // namespace isolated_devmgr
