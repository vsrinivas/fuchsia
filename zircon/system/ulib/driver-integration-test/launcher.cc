// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-launcher/launch.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/zx/vmo.h>
#include <memory.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>

namespace driver_integration_test {

namespace {

// This board driver knows how to interpret the metadata for which devices to
// spawn.
const zbi_platform_id_t kPlatformId = []() {
  zbi_platform_id_t plat_id = {};
  plat_id.vid = PDEV_VID_TEST;
  plat_id.pid = PDEV_PID_INTEGRATION_TEST;
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
zx_status_t GetBootItem(const fbl::Vector<board_test::DeviceEntry>& entries, fbl::String board_name,
                        uint32_t type, uint32_t extra, zx::vmo* out, uint32_t* length) {
  zx::vmo vmo;
  switch (type) {
    case ZBI_TYPE_PLATFORM_ID: {
      zbi_platform_id_t platform_id = kPlatformId;
      if (!board_name.empty()) {
        strncpy(platform_id.board_name, board_name.c_str(), ZBI_BOARD_NAME_LEN - 1);
      }
      zx_status_t status = zx::vmo::create(sizeof(kPlatformId), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&platform_id, 0, sizeof(platform_id));
      if (status != ZX_OK) {
        return status;
      }
      *length = sizeof(platform_id);
      break;
    }
    case ZBI_TYPE_DRV_BOARD_INFO: {
      zbi_board_info_t board_info = kBoardInfo;
      zx_status_t status = zx::vmo::create(sizeof(kBoardInfo), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&board_info, 0, sizeof(board_info));
      if (status != ZX_OK) {
        return status;
      }
      *length = sizeof(board_info);
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

__EXPORT
zx_status_t IsolatedDevmgr::Create(IsolatedDevmgr::Args* args, IsolatedDevmgr* out) {
  IsolatedDevmgr devmgr;

  struct Args {
    Args(fbl::Vector<board_test::DeviceEntry> device_list, fbl::String board_name)
        : device_list_(std::move(device_list)), board_name_(board_name) {}
    fbl::Vector<board_test::DeviceEntry> device_list_;
    fbl::String board_name_;
  };
  auto cb_args = std::make_unique<Args>(std::move(args->device_list), args->board_name);

  devmgr_launcher::Args devmgr_args;
  devmgr_args.sys_device_driver = "/boot/driver/platform-bus.so";
  devmgr_args.driver_search_paths.swap(args->driver_search_paths);
  devmgr_args.load_drivers.swap(args->load_drivers);
  devmgr_args.flat_namespace = std::move(args->flat_namespace);
  devmgr_args.boot_args = std::move(args->boot_args);
  devmgr_args.disable_block_watcher = args->disable_block_watcher;
  devmgr_args.disable_netsvc = args->disable_netsvc;
  devmgr_args.no_exit_after_suspend = args->no_exit_after_suspend;
  devmgr_args.get_boot_item = [args = std::move(cb_args)](uint32_t type, uint32_t extra,
                                                          zx::vmo* out, uint32_t* length) {
    return GetBootItem(args->device_list_, args->board_name_, type, extra, out, length);
  };

  zx_status_t status =
      devmgr_integration_test::IsolatedDevmgr::Create(std::move(devmgr_args), &devmgr.devmgr_);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(devmgr);
  return ZX_OK;
}

}  // namespace driver_integration_test
