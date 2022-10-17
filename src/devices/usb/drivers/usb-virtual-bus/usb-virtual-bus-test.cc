// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.virtual.bus/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.virtualbustest/cpp/wire.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <lib/zx/clock.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <fbl/string.h>
#include <usb/cdc.h>
#include <usb/usb.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {

using usb_virtual::BusLauncher;

namespace virtualbustest = fuchsia_hardware_usb_virtualbustest;

constexpr const char kManufacturer[] = "Google";
constexpr const char kProduct[] = "USB Virtual Bus Virtual Device";
constexpr const char kSerial[] = "ebfd5ad49d2a";

zx_status_t WaitForDevice(int dirfd, int event, const char* name, void* cookie) {
  if (std::string_view{name} == ".") {
    return ZX_OK;
  }
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  const fdio_cpp::UnownedFdioCaller caller{dirfd};
  zx::result channel = component::ConnectAt<virtualbustest::BusTest>(caller.directory(), name);
  if (channel.is_error()) {
    return channel.status_value();
  }
  static_cast<fidl::WireSyncClient<virtualbustest::BusTest>*>(cookie)->Bind(
      std::move(channel.value()));
  return ZX_ERR_STOP;
}

class VirtualBusTest : public zxtest::Test {
 public:
  void SetUp() override {
    auto bus = BusLauncher::Create();
    ASSERT_OK(bus.status_value());
    bus_ = std::move(bus.value());
    ASSERT_NO_FATAL_FAILURE(InitUsbVirtualBus(&test_));
  }

  void TearDown() override {
    ASSERT_OK(bus_->ClearPeripheralDeviceFunctions());
    ASSERT_OK(bus_->Disable());
  }

 protected:
  void InitUsbVirtualBus(fidl::WireSyncClient<virtualbustest::BusTest>* test);

  std::optional<BusLauncher> bus_;
  fidl::WireSyncClient<virtualbustest::BusTest> test_;
};

void VirtualBusTest::InitUsbVirtualBus(fidl::WireSyncClient<virtualbustest::BusTest>* test) {
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
  config_descs.emplace_back(
      fidl::VectorView<usb_peripheral::wire::FunctionDescriptor>::FromExternal(function_descs));

  ASSERT_OK(bus_->SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

  fbl::unique_fd fd(openat(bus_->GetRootFd(), "class/virtual-bus-test", O_RDONLY));

  while (true) {
    auto result = fdio_watch_directory(fd.get(), WaitForDevice, ZX_TIME_INFINITE, test);
    if (result == ZX_ERR_STOP) {
      break;
    }
    // If we see ZX_ERR_INTERNAL, something wrong happens while waiting for devices.
    ASSERT_NE(result, ZX_ERR_INTERNAL);
  }
}

TEST_F(VirtualBusTest, ShortTransfer) {
  ASSERT_TRUE(test_->RunShortPacketTest().value().success);
  ASSERT_NO_FATAL_FAILURE();
}

}  // namespace
}  // namespace usb_virtual_bus
