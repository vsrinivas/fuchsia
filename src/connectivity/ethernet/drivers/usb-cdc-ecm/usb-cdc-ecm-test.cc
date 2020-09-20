// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/ethernet/llcpp/fidl.h>
#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
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
#include <zircon/device/ethernet.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/syscalls.h>

#include <vector>

#include <ddk/platform-defs.h>
#include <fbl/string.h>
#include <hid/boot.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {
namespace ethernet = ::llcpp::fuchsia::hardware::ethernet;
constexpr const char kManufacturer[] = "Google";
constexpr const char kProduct[] = "CDC Ethernet";
constexpr const char kSerial[] = "ebfd5ad49d2a";

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus() = default;

  void InitUsbCdcEcm(std::string* devpath, std::string* devpath1);
};

struct DevicePaths {
  std::string peripheral_path;
  std::string host_path;
  int dirfd;
};

zx_status_t GetTopologicalPath(int fd, std::string* out) {
  size_t path_len;
  fdio_cpp::UnownedFdioCaller connection(fd);
  auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
      zx::unowned_channel(connection.borrow_channel()));
  zx_status_t status = resp.status();
  if (status != ZX_OK) {
    return status;
  }
  if (resp->result.is_err()) {
    return ZX_ERR_NOT_FOUND;
  } else {
    auto& r = resp->result.response();
    path_len = r.path.size();
    if (path_len > PATH_MAX) {
      return ZX_ERR_INTERNAL;
    }
    *out = std::string(r.path.data(), r.path.size());
  }
  return ZX_OK;
}

zx_status_t WaitForHostAndPeripheral(int dirfd, int event, const char* name, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  DevicePaths* device_paths = reinterpret_cast<DevicePaths*>(cookie);
  std::string path(name);
  path = "class/ethernet/" + path;
  fbl::unique_fd dev_fd(openat(device_paths->dirfd, path.c_str(), O_RDONLY));
  std::string topological_path;
  auto status = GetTopologicalPath(dev_fd.get(), &topological_path);
  if (status != ZX_OK) {
    return status;
  }
  if (topological_path.find("/usb-peripheral/function-001") != std::string::npos) {
    device_paths->peripheral_path = path;
  } else if (topological_path.find("/usb-bus/") != std::string::npos) {
    device_paths->host_path = path;
  }
  if (device_paths->host_path.size() && device_paths->peripheral_path.size()) {
    return ZX_ERR_STOP;
  }
  return ZX_OK;
}

void USBVirtualBus::InitUsbCdcEcm(std::string* peripheral_path, std::string* host_path) {
  namespace usb_peripheral = ::llcpp::fuchsia::hardware::usb::peripheral;
  using ConfigurationDescriptor =
      ::fidl::VectorView<::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor>;
  usb_peripheral::DeviceDescriptor device_desc = {};
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

  usb_peripheral::FunctionDescriptor usb_cdc_ecm_function_desc = {
      .interface_class = USB_CLASS_COMM,
      .interface_subclass = USB_CDC_SUBCLASS_ETHERNET,
      .interface_protocol = 0,
  };

  std::vector<usb_peripheral::FunctionDescriptor> function_descs;
  function_descs.push_back(usb_cdc_ecm_function_desc);
  std::vector<ConfigurationDescriptor> config_descs;
  config_descs.emplace_back(fidl::unowned_vec(function_descs));
  config_descs.emplace_back(fidl::unowned_vec(function_descs));

  ASSERT_NO_FATAL_FAILURES(SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

  fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/ethernet", O_RDONLY));
  DevicePaths device_paths;
  device_paths.dirfd = devmgr_.devfs_root().get();
  while (true) {
    auto result =
        fdio_watch_directory(fd.get(), WaitForHostAndPeripheral, ZX_TIME_INFINITE, &device_paths);
    if (result == ZX_ERR_STOP) {
      break;
    }
    // If we see ZX_ERR_INTERNAL, something wrong happens while waiting for devices.
    ASSERT_NE(result, ZX_ERR_INTERNAL);
  }
  *peripheral_path = device_paths.peripheral_path;
  *host_path = device_paths.host_path;
}

class EthernetInterface {
 public:
  EthernetInterface(fbl::unique_fd fd) {
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
    mtu_ = info.mtu;
    // Calculate optimal size of VMO, and set up RX and TX buffers.
    size_t optimal_vmo_size = (fifos->rx_depth * mtu_) + (fifos->tx_depth * mtu_);
    zx::vmo vmo;
    ASSERT_OK(mapper_.CreateAndMap(optimal_vmo_size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                   nullptr, &vmo));
    io_buffer_ = static_cast<uint8_t*>(mapper_.start());
    io_buffer_size_ = optimal_vmo_size;
    auto set_io_buffer_result = ethernet_client_->SetIOBuffer(std::move(vmo));
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

  size_t mtu() { return mtu_; }

  uint32_t tx_depth() { return tx_depth_; }

 private:
  std::unique_ptr<ethernet::Device::SyncClient> ethernet_client_;
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
    ASSERT_NO_FATAL_FAILURES(bus_.InitUsbCdcEcm(&peripheral_path_, &host_path_));
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURES(bus_.ClearPeripheralDeviceFunctions());
    ASSERT_NO_FATAL_FAILURES(ValidateResult(bus_.virtual_bus().Disable()));
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

  ASSERT_NO_FATAL_FAILURES();
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

  ASSERT_NO_FATAL_FAILURES();
}

}  // namespace
}  // namespace usb_virtual_bus
