// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.ethernet/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/device/ethernet.h>
#include <zircon/syscalls.h>

#include <vector>

#include <fbl/string.h>
#include <hid/boot.h>
#include <usb/cdc.h>
#include <usb/usb.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {
namespace ethernet = fuchsia_hardware_ethernet;
constexpr const char kManufacturer[] = "Google";
constexpr const char kProduct[] = "CDC Ethernet";
constexpr const char kSerial[] = "ebfd5ad49d2a";

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
  std::optional<std::string> peripheral_path;
  std::optional<std::string> host_path;
};

zx_status_t WaitForHostAndPeripheral([[maybe_unused]] int dirfd, int event, const char* name,
                                     void* cookie) {
  if (std::string_view{name} == ".") {
    return ZX_OK;
  }
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  fbl::unique_fd dev_fd(openat(dirfd, name, O_RDONLY));
  std::string topological_path;
  zx_status_t status = GetTopologicalPath(dev_fd.get(), &topological_path);
  if (status != ZX_OK) {
    return status;
  }
  DevicePaths& device_paths = *reinterpret_cast<DevicePaths*>(cookie);
  if (topological_path.find("/usb-peripheral/function-001") != std::string::npos) {
    device_paths.peripheral_path.emplace(name);
  } else if (topological_path.find("/usb-bus/") != std::string::npos) {
    device_paths.host_path.emplace(name);
  }
  if (device_paths.host_path.has_value() && device_paths.peripheral_path.has_value()) {
    return ZX_ERR_STOP;
  }
  return ZX_OK;
}

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus() = default;

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

    ASSERT_NO_FATAL_FAILURE(SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

    constexpr char kClassEthernet[] = "class/ethernet";
    fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), kClassEthernet, O_RDONLY));
    DevicePaths device_paths;
    ASSERT_STATUS(
        fdio_watch_directory(fd.get(), WaitForHostAndPeripheral, ZX_TIME_INFINITE, &device_paths),
        ZX_ERR_STOP);
    ASSERT_TRUE(device_paths.peripheral_path.has_value());
    *peripheral_path =
        std::string(kClassEthernet) + '/' + std::move(device_paths.peripheral_path.value());
    ASSERT_TRUE(device_paths.host_path.has_value());
    *host_path = std::string(kClassEthernet) + '/' + std::move(device_paths.host_path.value());
  }
};

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

class UsbCdcEcmTest : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(bus_.InitUsbCdcEcm(&peripheral_path_, &host_path_));
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURE(bus_.ClearPeripheralDeviceFunctions());
    ASSERT_NO_FATAL_FAILURE(ValidateResult(bus_.virtual_bus()->Disable()));
  }

 protected:
  USBVirtualBus bus_;
  std::string peripheral_path_;
  std::string host_path_;
};

// Test sending data from peripheral to host.
TEST_F(UsbCdcEcmTest, PeripheralTransmitsToHost) {
  EthernetInterface peripheral_ethernet(
      fbl::unique_fd(openat(bus_.GetRootFd(), peripheral_path_.c_str(), O_RDWR)));
  EthernetInterface host_ethernet(
      fbl::unique_fd(openat(bus_.GetRootFd(), host_path_.c_str(), O_RDWR)));
  uint8_t fill_data = 0;
  for (size_t i = 0; i < peripheral_ethernet.tx_depth(); i++) {
    std::vector<uint8_t> data;
    for (size_t j = 0; j < peripheral_ethernet.mtu(); j++) {
      data.push_back(fill_data);
      fill_data++;
    }
    ASSERT_OK(peripheral_ethernet.SendData(data));
  }

  std::vector<uint8_t> received_data;
  while (received_data.size() < peripheral_ethernet.tx_depth() * peripheral_ethernet.mtu()) {
    ASSERT_OK(host_ethernet.ReceiveData(received_data));
  }
  fill_data = 0;
  for (size_t i = 0; i < received_data.size(); i++) {
    ASSERT_EQ(received_data[i], fill_data);
    fill_data++;
  }

  ASSERT_NO_FATAL_FAILURE();
}

// Test sending data from host to peripheral.
TEST_F(UsbCdcEcmTest, HostTransmitsToPeripheral) {
  EthernetInterface peripheral_ethernet(
      fbl::unique_fd(openat(bus_.GetRootFd(), peripheral_path_.c_str(), O_RDWR)));
  EthernetInterface host_ethernet(
      fbl::unique_fd(openat(bus_.GetRootFd(), host_path_.c_str(), O_RDWR)));
  uint8_t fill_data = 0;
  for (size_t i = 0; i < host_ethernet.tx_depth(); i++) {
    std::vector<uint8_t> data;
    for (size_t j = 0; j < host_ethernet.mtu(); j++) {
      data.push_back(fill_data);
      fill_data++;
    }
    ASSERT_OK(host_ethernet.SendData(data));
  }

  std::vector<uint8_t> received_data;
  while (received_data.size() < host_ethernet.tx_depth() * host_ethernet.mtu()) {
    ASSERT_OK(peripheral_ethernet.ReceiveData(received_data));
  }
  fill_data = 0;
  for (size_t i = 0; i < received_data.size(); i++) {
    ASSERT_EQ(received_data[i], fill_data);
    fill_data++;
  }

  ASSERT_NO_FATAL_FAILURE();
}

}  // namespace
}  // namespace usb_virtual_bus
