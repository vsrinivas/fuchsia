// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.ethernet/cpp/wire.h>
#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/device/ethernet.h>
#include <zircon/device/network.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <vector>

#include <fbl/string.h>
#include <hid/boot.h>
#include <usb/cdc.h>
#include <usb/usb.h>
#include <zxtest/zxtest.h>

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"

namespace usb_virtual_bus {
namespace {

using driver_integration_test::IsolatedDevmgr;
using fuchsia::diagnostics::Severity;
using fuchsia::driver::test::DriverLog;
using usb_virtual::BusLauncher;

namespace ethernet = fuchsia_hardware_ethernet;
constexpr const char kManufacturer[] = "Google";
constexpr const char kProduct[] = "CDC Ethernet";
constexpr const char kSerial[] = "ebfd5ad49d2a";
constexpr size_t kEthernetMtu = 1500;

zx_status_t GetTopologicalPath(int fd, std::string* out) {
  size_t path_len;
  fdio_cpp::UnownedFdioCaller connection(fd);
  auto resp = fidl::WireCall(
                  ::fidl::UnownedClientEnd<fuchsia_device::Controller>(connection.borrow_channel()))
                  ->GetTopologicalPath();
  zx_status_t status = resp.status();
  if (status != ZX_OK) {
    return status;
  }
  if (resp.value().is_error()) {
    return resp.value().error_value();
  }
  const auto& r = *resp->value();
  path_len = r.path.size();
  if (path_len > PATH_MAX) {
    return ZX_ERR_INTERNAL;
  }
  *out = std::string(r.path.data(), r.path.size());
  return ZX_OK;
}

struct DevicePaths {
  std::optional<std::string> path;
  std::string subdir;
  std::string query;
};

zx_status_t WaitForDevice(int dirfd, int event, const char* name, void* cookie) {
  if (std::string_view{name} == ".") {
    return ZX_OK;
  }
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  fbl::unique_fd fd(openat(dirfd, name, O_RDONLY));
  if (!fd.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }
  std::string topological_path;
  zx_status_t status = GetTopologicalPath(fd.get(), &topological_path);
  if (status != ZX_OK) {
    return status;
  }
  DevicePaths* paths = reinterpret_cast<DevicePaths*>(cookie);
  if (topological_path.find(paths->query) != std::string::npos) {
    paths->path = paths->subdir + std::string{name};
    return ZX_ERR_STOP;
  }
  return ZX_OK;
}

class EthernetInterface {
 public:
  explicit EthernetInterface(fbl::unique_fd fd) {
    zx::status client_end = fdio_cpp::FdioCaller(std::move(fd)).take_as<ethernet::Device>();
    ASSERT_OK(client_end.status_value());
    ethernet_client_.Bind(std::move(*client_end));
    // Get device information
    auto get_info_result = ethernet_client_->GetInfo();
    ASSERT_OK(get_info_result.status());
    auto info = get_info_result->info;
    auto get_fifos_result = ethernet_client_->GetFifos();
    ASSERT_OK(get_fifos_result.status());
    auto fifos = get_fifos_result->info.get();
    mtu_ = info.mtu;
    // Calculate optimal size of VMO, and set up RX and TX buffers.
    size_t optimal_vmo_size = (fifos->rx_depth * mtu_) + (fifos->tx_depth * mtu_);
    zx::vmo vmo;
    ASSERT_OK(mapper_.CreateAndMap(optimal_vmo_size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                   nullptr, &vmo));
    io_buffer_ = static_cast<uint8_t*>(mapper_.start());
    io_buffer_size_ = optimal_vmo_size;
    auto set_io_buffer_result = ethernet_client_->SetIoBuffer(std::move(vmo));
    ASSERT_OK(set_io_buffer_result.status());
    auto start_result = ethernet_client_->Start();
    ASSERT_OK(start_result.status());
    tx_fifo_ = std::move(fifos->tx);
    rx_fifo_ = std::move(fifos->rx);
    tx_depth_ = fifos->tx_depth;
    rx_depth_ = fifos->rx_depth;
    io_buffer_offset_ = rx_depth_ * mtu_;

    // Give all RX entries to the Ethernet driver
    size_t count;
    rx_entries_ = std::make_unique<eth_fifo_entry_t[]>(rx_depth_);
    for (size_t i = 0; i < rx_depth_; i++) {
      rx_entries_[i].offset = static_cast<uint32_t>(i * mtu_);
      rx_entries_[i].length = static_cast<uint16_t>(mtu_);
      rx_entries_[i].flags = 0;
      rx_entries_[i].cookie = 0;
    }
    ASSERT_OK(rx_fifo_.write(sizeof(eth_fifo_entry_t), rx_entries_.get(), rx_depth_, &count));
    ASSERT_EQ(count, rx_depth_);
  }

  // Receive data into a buffer. User is expected to call this function repeatedly until enough
  // data is read or status other than ZX_OK is returned. Note that the receive data buffer would
  // not affect the actual IO buffer.
  zx_status_t ReceiveData(std::vector<uint8_t>& data) {
    zx_status_t status;
    zx_signals_t signals;

    if ((status = rx_fifo_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                    zx::deadline_after(zx::sec(10)), &signals)) < 0) {
      if (!(signals & ZX_FIFO_READABLE)) {
        return status;
      }
    }
    size_t count;
    if ((status = rx_fifo_.read(sizeof(eth_fifo_entry_t), rx_entries_.get(), rx_depth_, &count)) <
        0) {
      return status;
    }
    for (eth_fifo_entry_t* e = rx_entries_.get(); count-- > 0; e++) {
      for (uint8_t* i = io_buffer_ + e->offset; i < io_buffer_ + e->offset + e->length; i++) {
        data.push_back(*i);
      }
    }

    return ZX_OK;
  }

  // Send data from a buffer.The data to be sent would be copied to the IO buffer and a new
  // fifo_entry would be created to send the data.
  zx_status_t SendData(std::vector<uint8_t> data) {
    uint16_t length = static_cast<uint16_t>(data.size());
    if (io_buffer_offset_ > io_buffer_size_ || io_buffer_offset_ > UINT32_MAX) {
      // This should not happen.
      return ZX_ERR_INTERNAL;
    }
    memcpy(io_buffer_ + io_buffer_offset_, data.data(), data.size());

    eth_fifo_entry_t e = {
        .offset = static_cast<uint32_t>(io_buffer_offset_),
        .length = length,
        .flags = 0,
        .cookie = 1,
    };
    io_buffer_offset_ += data.size();

    return tx_fifo_.write(sizeof(e), &e, 1, nullptr);
  }

  size_t mtu() const { return mtu_; }

  uint32_t tx_depth() const { return tx_depth_; }

  uint32_t rx_depth() const { return rx_depth_; }

 private:
  fidl::WireSyncClient<ethernet::Device> ethernet_client_;
  zx::fifo tx_fifo_;
  zx::fifo rx_fifo_;
  uint32_t tx_depth_;
  uint32_t rx_depth_;
  size_t mtu_;
  std::unique_ptr<eth_fifo_entry_t[]> rx_entries_;
  uint8_t* io_buffer_ = nullptr;
  size_t io_buffer_size_ = 0;
  size_t io_buffer_offset_ = 0;
  fzl::VmoMapper mapper_;
};

class NetworkDeviceInterface : public ::loop_fixture::RealLoop {
 public:
  explicit NetworkDeviceInterface(fbl::unique_fd fd) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
    ASSERT_OK(endpoints.status_value());
    auto [client_end, server_end] = *std::move(endpoints);

    zx::status<fidl::ClientEnd<fuchsia_hardware_network::DeviceInstance>> status =
        fdio_cpp::FdioCaller(std::move(fd)).take_as<fuchsia_hardware_network::DeviceInstance>();
    ASSERT_OK(status.status_value());
    fidl::WireClient<fuchsia_hardware_network::DeviceInstance> wire_client(
        std::move(status.value()), dispatcher());
    fidl::Status device_status = wire_client->GetDevice(std::move(server_end));
    ASSERT_OK(device_status.status());

    netdevice_client_.emplace(std::move(client_end), dispatcher());
    network::client::NetworkDeviceClient& client = netdevice_client_.value();
    {
      std::optional<zx_status_t> result;
      client.OpenSession("usb-cdc-ecm-test", [&result](zx_status_t status) { result = status; });
      RunLoopUntil([&result] { return result.has_value(); });
      ASSERT_OK(result.value());
    }
    ASSERT_TRUE(client.HasSession());
    client.SetRxCallback([&](network::client::NetworkDeviceClient::Buffer buf) {
      rx_queue_.emplace(std::move(buf));
    });
    {
      client.GetPorts(
          [this](zx::status<std::vector<network::client::netdev::wire::PortId>> ports_status) {
            ASSERT_OK(ports_status.status_value());
            std::vector<network::client::netdev::wire::PortId> ports =
                std::move(ports_status.value());
            ASSERT_EQ(ports.size(), 1);
            port_id_ = ports[0];
          });
      RunLoopUntil([this] { return port_id_.has_value(); });
    }
    {
      std::optional<zx_status_t> result;
      client.AttachPort(port_id_.value(), {fuchsia_hardware_network::wire::FrameType::kEthernet},
                        [&result](zx_status_t status) { result = status; });
      RunLoopUntil([&result] { return result.has_value(); });
      ASSERT_OK(result.value());
    }

    network::client::DeviceInfo device_info = client.device_info();
    tx_depth_ = device_info.tx_depth;
    rx_depth_ = device_info.rx_depth;

    {
      bool checked_mtu = false;
      zx::status<std::unique_ptr<network::client::NetworkDeviceClient::StatusWatchHandle>> result =
          client.WatchStatus(
              port_id_.value(),
              [this, &checked_mtu](fuchsia_hardware_network::wire::PortStatus status) {
                if (status.has_mtu()) {
                  mtu_ = status.mtu();
                }
                checked_mtu = true;
              });
      RunLoopUntil([&checked_mtu] { return checked_mtu; });
      ASSERT_TRUE(mtu_.has_value());
    }
  }

  zx_status_t SendData(std::vector<uint8_t>& data) {
    network::client::NetworkDeviceClient::Buffer buffer = netdevice_client_.value().AllocTx();
    if (!buffer.is_valid()) {
      return ZX_ERR_NO_MEMORY;
    }

    // Populate the buffer data and metadata
    buffer.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    buffer.data().SetPortId(port_id_.value());
    EXPECT_EQ(buffer.data().Write(data.data(), data.size()), data.size());
    zx_status_t status = buffer.Send();

    // Run the loop to give the Netdevice client an opportunity to send.
    RunLoopUntilIdle();

    return status;
  }

  zx::status<std::vector<uint8_t>> ReceiveData() {
    // Wait for the read callback registered with the Netdevice client to fill the queue.
    RunLoopUntil([&] { return !rx_queue_.empty(); });

    network::client::NetworkDeviceClient::Buffer buffer = std::move(rx_queue_.front());
    rx_queue_.pop();
    if (!buffer.is_valid()) {
      return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
    }
    // Check that the port ID and frame type match what we expect.
    if ((buffer.data().port_id().base != port_id_.value().base) ||
        (buffer.data().port_id().salt != port_id_.value().salt) ||
        (buffer.data().frame_type() != fuchsia_hardware_network::wire::FrameType::kEthernet)) {
      ADD_FAILURE(
          "Frame metadata does not match. Received frame port ID base: %hu \
                  Expected base: %hu \
                  Received frame port ID salt: %hu \
                  Expected salt: %hu \
                  Received frame type: %hu\
                  Expected frame type: %hu",
          buffer.data().port_id().base, port_id_.value().base, buffer.data().port_id().salt,
          buffer.data().port_id().salt, buffer.data().frame_type(),
          fuchsia_hardware_network::wire::FrameType::kEthernet);
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    std::vector<uint8_t> output;
    output.resize(buffer.data().len());
    size_t read = buffer.data().Read(output.data(), output.size());
    if (read != output.size()) {
      return zx::error(ZX_ERR_INTERNAL);
    }
    return zx::ok(std::move(output));
  }

  uint32_t tx_depth() { return tx_depth_; }

  uint32_t rx_depth() { return rx_depth_; }

  size_t mtu() { return mtu_.value(); }

 private:
  std::optional<network::client::NetworkDeviceClient> netdevice_client_;
  // Since the client receives data in a callback, we need to store the frames we receive.
  std::queue<network::client::NetworkDeviceClient::Buffer> rx_queue_;

  std::optional<network::client::netdev::wire::PortId> port_id_;
  uint32_t tx_depth_;
  uint32_t rx_depth_;
  std::optional<size_t> mtu_;
};

class UsbCdcEcmTest : public zxtest::Test {
 public:
  void SetUp() override {
    IsolatedDevmgr::Args args = {
        .log_level =
            {
                DriverLog{
                    .name = "ethernet_usb_cdc_ecm",
                    .log_level = Severity::DEBUG,
                },
                DriverLog{
                    .name = "usb_cdc_acm_function",
                    .log_level = Severity::DEBUG,
                },
            },
    };
    auto bus = BusLauncher::Create(std::move(args));
    ASSERT_OK(bus.status_value());
    bus_ = std::move(bus.value());

    ASSERT_NO_FATAL_FAILURE(InitUsbCdcEcm(&peripheral_path_, &host_path_));
  }

  void TearDown() override {
    ASSERT_OK(bus_->ClearPeripheralDeviceFunctions());
    ASSERT_OK(bus_->Disable());
  }

 protected:
  std::optional<BusLauncher> bus_;
  std::string peripheral_path_;
  std::string host_path_;

  void InitUsbCdcEcm(std::string* peripheral_path, std::string* host_path) {
    namespace usb_peripheral = fuchsia_hardware_usb_peripheral;
    using ConfigurationDescriptor =
        ::fidl::VectorView<fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor>;
    usb_peripheral::wire::DeviceDescriptor device_desc = {};
    device_desc.bcd_usb = htole16(0x0200);
    device_desc.b_device_class = 0;
    device_desc.b_device_sub_class = 0;
    device_desc.b_device_protocol = 0;
    device_desc.b_max_packet_size0 = 64;
    device_desc.bcd_device = htole16(0x0100);
    device_desc.b_num_configurations = 2;

    device_desc.manufacturer = fidl::StringView(kManufacturer);
    device_desc.product = fidl::StringView(kProduct);
    device_desc.serial = fidl::StringView(kSerial);

    device_desc.id_vendor = htole16(0x0BDA);
    device_desc.id_product = htole16(0x8152);

    usb_peripheral::wire::FunctionDescriptor usb_cdc_ecm_function_desc = {
        .interface_class = USB_CLASS_COMM,
        .interface_subclass = USB_CDC_SUBCLASS_ETHERNET,
        .interface_protocol = 0,
    };

    std::vector<usb_peripheral::wire::FunctionDescriptor> function_descs;
    function_descs.push_back(usb_cdc_ecm_function_desc);
    std::vector<ConfigurationDescriptor> config_descs;
    config_descs.emplace_back(
        fidl::VectorView<usb_peripheral::wire::FunctionDescriptor>::FromExternal(function_descs));
    config_descs.emplace_back(
        fidl::VectorView<usb_peripheral::wire::FunctionDescriptor>::FromExternal(function_descs));

    ASSERT_OK(bus_->SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

    const auto wait_for_device = [this](DevicePaths& paths) {
      fbl::unique_fd fd(openat(bus_->GetRootFd(), paths.subdir.c_str(), O_RDONLY));
      ASSERT_TRUE(fd.is_valid());
      ASSERT_STATUS(fdio_watch_directory(fd.get(), WaitForDevice, ZX_TIME_INFINITE, &paths),
                    ZX_ERR_STOP);
    };
    DevicePaths host_device_paths{.subdir = "class/network/", .query = "/usb-bus/"};
    // Attach to function-001, because it implements usb-cdc-ecm.
    DevicePaths peripheral_device_paths{.subdir = "class/ethernet/",
                                        .query = "/usb-peripheral/function-001"};

    wait_for_device(host_device_paths);
    wait_for_device(peripheral_device_paths);

    *host_path = host_device_paths.path.value();
    *peripheral_path = peripheral_device_paths.path.value();
  }
};

TEST_F(UsbCdcEcmTest, PeripheralTransmitsToHost) {
  EthernetInterface peripheral(
      fbl::unique_fd(openat(bus_->GetRootFd(), peripheral_path_.c_str(), O_RDWR)));
  NetworkDeviceInterface host(
      fbl::unique_fd(openat(bus_->GetRootFd(), host_path_.c_str(), O_RDWR)));

  const uint32_t fifo_depth = std::min(peripheral.tx_depth(), host.rx_depth());
  ASSERT_EQ(peripheral.mtu(), kEthernetMtu);
  ASSERT_EQ(host.mtu(), kEthernetMtu);

  uint8_t fill_data = 0;
  for (size_t i = 0; i < fifo_depth; ++i) {
    std::vector<uint8_t> data;
    for (size_t j = 0; j < kEthernetMtu; ++j) {
      data.push_back(fill_data++);
    }
    ASSERT_OK(peripheral.SendData(data));
  }

  size_t received_bytes = 0;
  uint8_t read_data = 0;
  while (received_bytes < fifo_depth * kEthernetMtu) {
    zx::status<std::vector<uint8_t>> received_data = host.ReceiveData();
    ASSERT_OK(received_data.status_value());
    ASSERT_EQ(kEthernetMtu, received_data.value().size());
    const std::vector<uint8_t>& data = received_data.value();
    received_bytes += data.size();
    for (const auto& b : data) {
      ASSERT_EQ(b, read_data++);
    }
  }
  ASSERT_NO_FATAL_FAILURE();
}

TEST_F(UsbCdcEcmTest, HostTransmitsToPeripheral) {
  EthernetInterface peripheral(
      fbl::unique_fd(openat(bus_->GetRootFd(), peripheral_path_.c_str(), O_RDWR)));
  NetworkDeviceInterface host(
      fbl::unique_fd(openat(bus_->GetRootFd(), host_path_.c_str(), O_RDWR)));

  const uint32_t fifo_depth = std::min(peripheral.rx_depth(), host.tx_depth());
  ASSERT_EQ(peripheral.mtu(), kEthernetMtu);
  ASSERT_EQ(host.mtu(), kEthernetMtu);

  uint8_t fill_data = 0;
  for (size_t i = 0; i < fifo_depth; ++i) {
    std::vector<uint8_t> data;
    for (size_t j = 0; j < kEthernetMtu; ++j) {
      data.push_back(fill_data++);
    }
    ASSERT_OK(host.SendData(data));
  }

  std::vector<uint8_t> received_data;
  while (received_data.size() < fifo_depth * kEthernetMtu) {
    ASSERT_OK(peripheral.ReceiveData(received_data));
  }
  for (size_t i = 0; i < received_data.size(); ++i) {
    ASSERT_EQ(received_data[i], i % 256);
  }
  ASSERT_NO_FATAL_FAILURE();
}

}  // namespace
}  // namespace usb_virtual_bus
