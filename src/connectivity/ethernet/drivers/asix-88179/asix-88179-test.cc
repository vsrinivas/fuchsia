// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/ax88179/llcpp/fidl.h>
#include <fuchsia/hardware/ethernet/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <lib/zx/clock.h>
#include <zircon/device/ethernet.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

#include <zxtest/zxtest.h>

#include "asix-88179-regs.h"

namespace usb_ax88179 {
namespace {

namespace ethernet = ::llcpp::fuchsia::hardware::ethernet;
namespace ax88179 = ::llcpp::fuchsia::hardware::ax88179;

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus() = default;

  void InitUsbAx88179(fbl::String* dev_path, fbl::String* test_function_path);
};

void USBVirtualBus::InitUsbAx88179(fbl::String* dev_path, fbl::String* test_function_path) {
  namespace usb_peripheral = ::llcpp::fuchsia::hardware::usb::peripheral;
  using ConfigurationDescriptor =
      ::fidl::VectorView<::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor>;

  usb_peripheral::DeviceDescriptor device_desc = {};
  device_desc.bcd_usb = htole16(0x0200);
  device_desc.b_max_packet_size0 = 64;
  device_desc.bcd_device = htole16(0x0100);
  device_desc.b_num_configurations = 1;

  usb_peripheral::FunctionDescriptor usb_ax88179_desc = {
      .interface_class = USB_CLASS_COMM,
      .interface_subclass = USB_CDC_SUBCLASS_ETHERNET,
      .interface_protocol = 0,
  };

  device_desc.id_vendor = htole16(ASIX_VID);
  device_desc.id_product = htole16(AX88179_PID);
  std::vector<ConfigurationDescriptor> config_descs;
  std::vector<usb_peripheral::FunctionDescriptor> function_descs;
  function_descs.push_back(usb_ax88179_desc);
  ConfigurationDescriptor config_desc;
  config_desc = fidl::VectorView(fidl::unowned_ptr(function_descs.data()), function_descs.size());
  config_descs.push_back(std::move(config_desc));
  ASSERT_NO_FATAL_FAILURES(SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

  fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/ethernet", O_RDONLY));
  while (fdio_watch_directory(fd.get(), usb_virtual_bus::WaitForAnyFile, ZX_TIME_INFINITE,
                              dev_path) != ZX_ERR_STOP) {
  }
  *dev_path = fbl::String::Concat({fbl::String("class/ethernet/"), *dev_path});

  fd.reset(openat(devmgr_.devfs_root().get(), "class/test-asix-function", O_RDONLY));
  while (fdio_watch_directory(fd.get(), usb_virtual_bus::WaitForAnyFile, ZX_TIME_INFINITE,
                              test_function_path) != ZX_ERR_STOP) {
  }
  *test_function_path =
      fbl::String::Concat({fbl::String("class/test-asix-function/"), *test_function_path});
}

class UsbAx88179Test : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURES(bus_.InitUsbAx88179(&dev_path_, &test_function_path_));
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURES(bus_.ClearPeripheralDeviceFunctions());

    auto result2 = bus_.virtual_bus().Disable();
    ASSERT_NO_FATAL_FAILURES(usb_virtual_bus::ValidateResult(result2));
  }

  void ConnectEthernetClient() {
    fbl::unique_fd fd(openat(bus_.GetRootFd(), dev_path_.c_str(), O_RDWR));
    zx::channel ethernet_handle;
    ASSERT_OK(fdio_get_service_handle(fd.release(), ethernet_handle.reset_and_get_address()));
    ethernet_client_.reset(new ethernet::Device::SyncClient(std::move(ethernet_handle)));

    // Get device information
    auto get_info_result = ethernet_client_->GetInfo();
    ASSERT_OK(get_info_result.status());
    auto info = get_info_result.Unwrap()->info;
    auto get_fifos_result = ethernet_client_->GetFifos();
    ASSERT_OK(get_fifos_result.status());
    auto fifos = get_fifos_result.Unwrap()->info.get();
    // Calculate optimal size of VMO, and set up RX and TX buffers.
    size_t optimal_vmo_size = (fifos->rx_depth * info.mtu) + (fifos->tx_depth * info.mtu);
    zx::vmo vmo;
    fzl::VmoMapper mapper;
    ASSERT_OK(mapper.CreateAndMap(optimal_vmo_size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                  nullptr, &vmo));
    uint8_t* io_buffer = nullptr;
    size_t io_buffer_size = 0;
    io_buffer = static_cast<uint8_t*>(mapper.start());
    io_buffer_size = optimal_vmo_size;
    auto set_io_buffer_result = ethernet_client_->SetIOBuffer(std::move(vmo));
    ASSERT_OK(set_io_buffer_result.status());

    rx_fifo_ = std::move(fifos->rx);
    tx_fifo_ = std::move(fifos->tx);
  }

  void StartDevice() {
    auto start_result = ethernet_client_->Start();
    ASSERT_OK(start_result.status());
  }

  void SetDeviceOnline() {
    fbl::unique_fd fd(openat(bus_.GetRootFd(), test_function_path_.c_str(), O_RDWR));
    zx::channel test_handle;
    ASSERT_OK(fdio_get_service_handle(fd.release(), test_handle.reset_and_get_address()));

    std::unique_ptr<ax88179::Hooks::SyncClient> test_client;
    test_client.reset(new ax88179::Hooks::SyncClient(std::move(test_handle)));

    // Ensure SIGNAL_STATUS is de-asserted before we set it.
    ASSERT_OK(ethernet_client_->GetStatus().status());

    auto online_result = test_client->SetOnline(true);
    ASSERT_OK(online_result.status());
    auto result = online_result.Unwrap()->status;
    ASSERT_OK(result);
  }

  ethernet::DeviceStatus GetDeviceStatus() {
    auto status_result = ethernet_client_->GetStatus();
    ZX_ASSERT(status_result.status() == ZX_OK);
    return status_result.Unwrap()->device_status;
  }

  void ExpectStatusOnline() {
    // Two attempts, as SIGNAL_STATUS is triggered by both ethernet.cc starting
    // up and also by our fake function driver. As we can't control the delivery
    // order, confirm that we get to online eventually.
    for (int tries = 0; tries < 2; ++tries) {
      zx_signals_t pending;
      ASSERT_OK(rx_fifo_.wait_one(ethernet::SIGNAL_STATUS, zx::time::infinite(), &pending));
      ASSERT_EQ((pending & ethernet::SIGNAL_STATUS), ethernet::SIGNAL_STATUS);
      if (GetDeviceStatus() & ethernet::DeviceStatus::ONLINE) {
        return;
      }
    }
    ADD_FATAL_FAILURE();
  }

 protected:
  USBVirtualBus bus_;
  fbl::String dev_path_;
  fbl::String test_function_path_;
  std::unique_ptr<ethernet::Device::SyncClient> ethernet_client_;
  zx::fifo rx_fifo_;
  zx::fifo tx_fifo_;
};

TEST_F(UsbAx88179Test, SetupShutdownTest) { ASSERT_NO_FATAL_FAILURES(); }

TEST_F(UsbAx88179Test, OfflineByDefault) {
  ASSERT_NO_FATAL_FAILURES(ConnectEthernetClient());

  ASSERT_NO_FATAL_FAILURES(StartDevice());

  ASSERT_FALSE(GetDeviceStatus() & ethernet::DeviceStatus::ONLINE);
}

TEST_F(UsbAx88179Test, SetOnlineAfterStart) {
  ASSERT_NO_FATAL_FAILURES(ConnectEthernetClient());

  ASSERT_NO_FATAL_FAILURES(StartDevice());

  ASSERT_NO_FATAL_FAILURES(SetDeviceOnline());

  ASSERT_NO_FATAL_FAILURES(ExpectStatusOnline());
}

// This is for https://fxbug.dev/40786#c41.
TEST_F(UsbAx88179Test, SetOnlineBeforeStart) {
  ASSERT_NO_FATAL_FAILURES(ConnectEthernetClient());

  ASSERT_NO_FATAL_FAILURES(SetDeviceOnline());

  ASSERT_NO_FATAL_FAILURES(StartDevice());

  ASSERT_NO_FATAL_FAILURES(ExpectStatusOnline());
}

}  // namespace
}  // namespace usb_ax88179
