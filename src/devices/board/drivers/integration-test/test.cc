// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fidl/fuchsia.board.test/cpp/wire.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/metadata/test.h>
#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/vector.h>

#include "src/devices/board/drivers/integration-test/test-bus-bind.h"

namespace board_test {

class TestBoard;
using TestBoardType = ddk::Device<TestBoard, ddk::Messageable<fuchsia_board_test::Board>::Mixin>;

// This is the main class for the platform bus driver.
class TestBoard : public TestBoardType {
 public:
  explicit TestBoard(zx_device_t* parent, pbus_protocol_t* pbus)
      : TestBoardType(parent), pbus_(pbus) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void CreateDevice(CreateDeviceRequestView request,
                    CreateDeviceCompleter::Sync& completer) override {
    pbus_dev_t device = {};
    device.name = request->entry.name.data();
    device.vid = request->entry.vid;
    device.pid = request->entry.pid;
    device.did = request->entry.did;

    zx_status_t status = pbus_.DeviceAdd(&device);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to add device: %s: %d", device.name, status);
    }

    completer.Reply();
  }

  // Device protocol implementation.
  void DdkRelease();

 private:
  TestBoard(const TestBoard&) = delete;
  TestBoard& operator=(const TestBoard&) = delete;
  TestBoard(TestBoard&&) = delete;
  TestBoard& operator=(TestBoard&&) = delete;

  // Fetches devices to load from metadata and deserializes into a vector of
  // pbus_dev_t.
  zx_status_t FetchAndDeserialize();
  zx_status_t Start();
  int Thread();

  fbl::Array<uint8_t> metadata_;

  fbl::Vector<pbus_metadata_t> devices_metadata_;
  fbl::Vector<pbus_dev_t> devices_;

  ddk::PBusProtocolClient pbus_;
  thrd_t thread_;
};

void TestBoard::DdkRelease() { delete this; }

// This function must be kept updated with the function that serializes the date.
// This function is driver_integration_test::GetBootItem.
zx_status_t TestBoard::FetchAndDeserialize() {
  size_t metadata_size;
  zx_status_t status = DdkGetMetadataSize(DEVICE_METADATA_BOARD_PRIVATE, &metadata_size);
  if (status != ZX_OK) {
    return status;
  }
  if (metadata_size < sizeof(DeviceList)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  fbl::AllocChecker ac;
  auto metadata = new (&ac) uint8_t[metadata_size];
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  metadata_.reset(metadata, metadata_size);

  size_t actual;
  status = DdkGetMetadata(DEVICE_METADATA_BOARD_PRIVATE, metadata, metadata_size, &actual);
  if (status != ZX_OK) {
    return status;
  }
  if (actual != metadata_size) {
    return ZX_ERR_INTERNAL;
  }

  const auto* device_list = reinterpret_cast<DeviceList*>(metadata);
  if (metadata_size < sizeof(DeviceList) + device_list->count * sizeof(DeviceEntry)) {
    return ZX_ERR_INTERNAL;
  }

  devices_.reserve(device_list->count, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  size_t metadata_offset = (device_list->count * sizeof(DeviceEntry)) + sizeof(DeviceList);
  for (size_t i = 0; i < device_list->count; i++) {
    const auto& entry = device_list->list[i];
    // Create the device.
    pbus_dev_t device = {};
    device.name = entry.name;
    device.vid = entry.vid;
    device.pid = entry.pid;
    device.did = entry.did;

    // Create the metadata.
    pbus_metadata_t metadata = {};
    metadata.type = DEVICE_METADATA_TEST;
    metadata.data_size = entry.metadata_size;
    metadata.data_buffer = metadata_.data() + metadata_offset;
    metadata_offset += metadata.data_size;

    // Store the metadata and link the device to it.
    devices_metadata_.push_back(std::move(metadata));
    device.metadata_count = 1;
    device.metadata_list = &devices_metadata_[devices_metadata_.size() - 1];

    devices_.push_back(device);
  }

  // Inform the platform bus of our bootloader info.
  // This is set to "coreboot" specifically for CrosDevicePartitionerTests.
  pbus_bootloader_info_t bootloader_info{.vendor = "coreboot"};
  status = pbus_.SetBootloaderInfo(&bootloader_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetBootloaderInfo failed: %d", status);
    return status;
  }

  return ZX_OK;
}

int TestBoard::Thread() {
  for (const auto& device : devices_) {
    zx_status_t status = pbus_.DeviceAdd(&device);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to add device.");
    }
  }
  return 0;
}

zx_status_t TestBoard::Start() {
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<TestBoard*>(arg)->Thread(); }, this,
      "test-board-start-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t TestBoard::Create(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;
  if (device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus) != ZX_OK) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto board = std::make_unique<TestBoard>(parent, &pbus);

  zx_status_t status = board->FetchAndDeserialize();
  if (status != ZX_OK) {
    zxlogf(ERROR, "TestBoard::Create: FetchAndDeserialize failed: %d", status);
    return status;
  }

  status = board->DdkAdd("test-board", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "TestBoard::Create: DdkAdd failed: %d", status);
    return status;
  }

  status = board->Start();
  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = board.release();
  }

  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestBoard::Create;
  return ops;
}();

}  // namespace board_test

ZIRCON_DRIVER(test_bus, board_test::driver_ops, "zircon", "0.1");
