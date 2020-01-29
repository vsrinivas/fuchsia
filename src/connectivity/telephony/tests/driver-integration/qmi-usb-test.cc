// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/telephony/transport/llcpp/fidl.h>
#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <lib/fdio/watcher.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/channel.h>
#include <lib/zx/port.h>
#include <unistd.h>
#include <zircon/hw/usb.h>

#include <fbl/string.h>
#include <gtest/gtest.h>
#include <src/lib/isolated_devmgr/usb-virtual-bus.h>

constexpr uint32_t kQmiBufSize = 2048LU;

namespace {

/// hardcoded QMI request/response
constexpr uint8_t kQmiImeiReq[]{1, 12, 0, 0, 2, 1, 0, 1, 0, 37, 0, 0, 0};
constexpr uint8_t kQmiImeiResp[]{1,  41, 0,  128, 2,  1,  2,  1,  0,  37, 0,  29, 0,  2,
                                 4,  0,  0,  0,   0,  0,  16, 1,  0,  48, 17, 15, 0,  51,
                                 53, 57, 50, 54,  48, 48, 56, 48, 49, 54, 56, 51, 53, 49};

const std::string isolated_devmgr_pkg_url =
    "fuchsia-pkg://fuchsia.com/tel_devmgr#meta/tel_devmgr.cmx";
const std::string isolated_devmgr_svc_name = "fuchsia.tel.devmgr.IsolatedDevmgr";

class USBVirtualBusQmi : public usb_virtual_bus::USBVirtualBusBase {
 public:
  USBVirtualBusQmi() : USBVirtualBusBase(isolated_devmgr_pkg_url, isolated_devmgr_svc_name) {}

  // Initialize QMI. Asserts on failure.
  void InitUsbQmi(fbl::String* devpath);
};

void USBVirtualBusQmi::InitUsbQmi(fbl::String* devpath) {
  namespace usb_peripheral = ::llcpp::fuchsia::hardware::usb::peripheral;
  usb_peripheral::DeviceDescriptor device_desc = {};
  device_desc.bcd_usb = htole16(0x0200);
  device_desc.b_device_class = 0;
  device_desc.b_device_sub_class = 0;
  device_desc.b_device_protocol = 0;
  device_desc.b_max_packet_size0 = 64;
  device_desc.bcd_device = htole16(0x0100);
  device_desc.b_num_configurations = 1;
  device_desc.id_vendor = htole16(0x1199);
  device_desc.id_product = htole16(0x9091);
  usb_peripheral::FunctionDescriptor qmi_function_desc = {
      .interface_class = USB_CLASS_VENDOR,
      .interface_subclass = USB_SUBCLASS_VENDOR,
      .interface_protocol = 0xff,
  };
  std::vector<usb_peripheral::FunctionDescriptor> function_descs_vec;
  function_descs_vec.push_back(qmi_function_desc);
  SetupPeripheralDevice(device_desc, std::move(function_descs_vec));
  fbl::unique_fd fd(openat(devfs_root().get(), "class/qmi-transport", O_RDONLY));
  while (fdio_watch_directory(fd.get(), usb_virtual_bus_helper::WaitForAnyFile, ZX_TIME_INFINITE,
                              devpath) != ZX_ERR_STOP)
    ;
  *devpath = fbl::String::Concat({fbl::String("class/qmi-transport/"), *devpath});
}

class UsbQmiTest : public ::gtest::RealLoopFixture {
 public:
  void SetUp() override {
    bus_.InitPeripheral();
    bus_.InitUsbQmi(&devpath_);
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURE(bus_.ClearPeripheralDeviceFunctions());

    auto result2 = bus_.virtual_bus().Disable();
    ASSERT_EQ(result2.status(), ZX_OK);
    ASSERT_EQ(result2.value().status, ZX_OK);
  }

  USBVirtualBusQmi& GetVirtBus() { return bus_; };

  fbl::String& GetDevPath() { return devpath_; };

 private:
  USBVirtualBusQmi bus_;
  fbl::String devpath_;
};

TEST_F(UsbQmiTest, RequestImei) {
  fbl::unique_fd fd_qmi(openat(GetVirtBus().GetRootFd(), GetDevPath().c_str(), O_RDWR));
  ASSERT_GT(fd_qmi.get(), 0);

  fdio_cpp::FdioCaller qmi_fdio_caller_;
  qmi_fdio_caller_.reset(std::move(fd_qmi));
  zx::port channel_port;
  // set QMI channel to driver
  zx::channel channel_local, channel_remote;
  ASSERT_EQ(zx::channel::create(0, &channel_local, &channel_remote), ZX_OK);
  auto result = llcpp::fuchsia::hardware::telephony::transport::Qmi::Call::SetChannel(
      qmi_fdio_caller_.channel(), std::move(channel_remote));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result->result.is_err(), false);
  ASSERT_EQ(zx::port::create(0, &channel_port), ZX_OK);
  channel_local.wait_async(channel_port, 0, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                           ZX_WAIT_ASYNC_ONCE);
  // send QMI message requesting IMEI
  ASSERT_EQ(channel_local.write(0, kQmiImeiReq, sizeof(kQmiImeiReq), nullptr, 0), ZX_OK);

  // validate QMI message for IMEI reply
  zx_port_packet_t packet;
  ASSERT_EQ(channel_port.wait(zx::time(ZX_TIME_INFINITE), &packet), ZX_OK);
  ASSERT_EQ(packet.key, 0LU);
  ASSERT_EQ(packet.signal.observed & ZX_CHANNEL_PEER_CLOSED, 0L);
  std::array<uint8_t, kQmiBufSize> buffer;
  uint32_t read_bytes;
  uint32_t handle_num;
  channel_local.read(0, buffer.data(), nullptr, kQmiBufSize, 0, &read_bytes, &handle_num);
  ASSERT_EQ(handle_num, 0LU);
  ASSERT_EQ(memcmp(buffer.data(), kQmiImeiResp, sizeof(kQmiImeiResp)), 0L);
}

}  // namespace
