// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtualbustest/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <lib/zx/clock.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/syscalls.h>

#include <ddk/platform-defs.h>
#include <fbl/string.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {
namespace virtualbustest = fuchsia_hardware_usb_virtualbustest;
constexpr const char kManufacturer[] = "Google";
constexpr const char kProduct[] = "USB Virtual Bus Virtual Device";
constexpr const char kSerial[] = "ebfd5ad49d2a";

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus() = default;

  void InitUsbVirtualBus(std::optional<virtualbustest::BusTest::SyncClient>* test);
};

zx_status_t WaitForDevice(int dirfd, int event, const char* name, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  fbl::unique_fd dev_fd(openat(dirfd, name, O_RDWR));
  zx::channel channel;
  fdio_get_service_handle(dev_fd.release(), channel.reset_and_get_address());
  auto* client = static_cast<std::optional<virtualbustest::BusTest::SyncClient>*>(cookie);
  client->emplace(std::move(channel));
  return ZX_ERR_STOP;
}

void USBVirtualBus::InitUsbVirtualBus(std::optional<virtualbustest::BusTest::SyncClient>* test) {
  namespace usb_peripheral = fuchsia_hardware_usb_peripheral;
  using ConfigurationDescriptor =
      ::fidl::VectorView<fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor>;

  usb_peripheral::wire::DeviceDescriptor device_desc = {};
  device_desc.bcd_usb = 0x0200;
  device_desc.b_device_class = 0;
  device_desc.b_device_sub_class = 0;
  device_desc.b_device_protocol = 0;
  device_desc.b_max_packet_size0 = 64;
  device_desc.bcd_device = 0x0100;
  device_desc.b_num_configurations = 1;

  device_desc.manufacturer = fidl::StringView(kManufacturer);
  device_desc.product = fidl::StringView(kProduct);
  device_desc.serial = fidl::StringView(kSerial);

  device_desc.id_vendor = 0x18D1;
  device_desc.id_product = 2;

  usb_peripheral::wire::FunctionDescriptor usb_cdc_ecm_function_desc = {
      .interface_class = USB_CLASS_VENDOR,
      .interface_subclass = 0,
      .interface_protocol = 0,
  };

  std::vector<usb_peripheral::wire::FunctionDescriptor> function_descs;
  function_descs.push_back(usb_cdc_ecm_function_desc);
  std::vector<ConfigurationDescriptor> config_descs;
  config_descs.emplace_back(fidl::unowned_vec(function_descs));

  ASSERT_NO_FATAL_FAILURES(SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

  fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/virtual-bus-test", O_RDONLY));

  while (true) {
    auto result = fdio_watch_directory(fd.get(), WaitForDevice, ZX_TIME_INFINITE, test);
    if (result == ZX_ERR_STOP) {
      break;
    }
    // If we see ZX_ERR_INTERNAL, something wrong happens while waiting for devices.
    ASSERT_NE(result, ZX_ERR_INTERNAL);
  }
}

class VirtualBusTest : public zxtest::Test {
 public:
  void SetUp() override { ASSERT_NO_FATAL_FAILURES(bus_.InitUsbVirtualBus(&test_)); }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURES(bus_.ClearPeripheralDeviceFunctions());
    ASSERT_NO_FATAL_FAILURES(ValidateResult(bus_.virtual_bus().Disable()));
  }

 protected:
  USBVirtualBus bus_;
  std::optional<virtualbustest::BusTest::SyncClient> test_;
};

TEST_F(VirtualBusTest, ShortTransfer) {
  ASSERT_TRUE(test_->RunShortPacketTest()->success);
  ASSERT_NO_FATAL_FAILURES();
}

}  // namespace
}  // namespace usb_virtual_bus
