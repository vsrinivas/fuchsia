// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-peripheral.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/clock.h>
#include <lib/zx/interrupt.h>
#include <zircon/errors.h>
#include <zircon/hw/usb.h>
#include <zircon/syscalls.h>

#include <cstring>
#include <list>
#include <map>
#include <memory>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/usb/dci.h>
#include <ddk/usb-peripheral-config.h>
#include <ddktl/protocol/usb/dci.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "usb-function.h"

struct zx_device : std::enable_shared_from_this<zx_device> {
  std::list<std::shared_ptr<zx_device>> devices;
  std::weak_ptr<zx_device> parent;
  std::vector<zx_device_prop_t> props;
  void* proto_ops;
  uint32_t proto_id;
  void* ctx;
  zx_protocol_device_t dev_ops;
  std::map<uint32_t, std::vector<uint8_t>> metadata;
  virtual ~zx_device() = default;
};

class FakeDevice : public ddk::UsbDciProtocol<FakeDevice, ddk::base_protocol> {
 public:
  FakeDevice() : proto_({&usb_dci_protocol_ops_, this}) {}

  // USB DCI protocol implementation.
  void UsbDciRequestQueue(usb_request_t* req, const usb_request_complete_t* cb) {}

  zx_status_t UsbDciSetInterface(const usb_dci_interface_protocol_t* interface) {
    interface_ = *interface;
    return ZX_OK;
  }

  zx_status_t UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                             const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbDciDisableEp(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t UsbDciEpSetStall(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t UsbDciEpClearStall(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }
  size_t UsbDciGetRequestSize() { return sizeof(usb_request_t); }

  zx_status_t UsbDciCancelAll(uint8_t ep_address) { return ZX_OK; }

  usb_dci_protocol_t* proto() { return &proto_; }

  usb_dci_interface_protocol_t* interface() { return &interface_; }

 private:
  usb_dci_interface_protocol_t interface_;
  usb_dci_protocol_t proto_;
};

static void DestroyDevices(zx_device_t* node) {
  for (auto& dev : node->devices) {
    DestroyDevices(dev.get());
    if (dev->dev_ops.unbind) {
      dev->dev_ops.unbind(dev->ctx);
    }
    dev->dev_ops.release(dev->ctx);
  }
}

const char kSerialNumber[] = "Test serial number";

class Ddk : public fake_ddk::Bind {
 public:
  Ddk() {
    UsbConfig config = {};
    memcpy(config.serial, kSerialNumber, sizeof(kSerialNumber));
    InsertMetadata(DEVICE_METADATA_USB_CONFIG, config);
    InsertMetadata(DEVICE_METADATA_SERIAL_NUMBER, kSerialNumber);
  }
  template <typename T>
  void InsertMetadata(uint32_t type, const T& value) {
    std::vector<uint8_t> data;
    data.resize(sizeof(T));
    memcpy(data.data(), &value, data.size());
    metadata_[type] = std::move(data);
  }
  zx_status_t DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* data, size_t length,
                                size_t* actual) override {
    if (metadata_.find(type) == metadata_.end()) {
      return ZX_ERR_NOT_FOUND;
    }
    if (metadata_[type].size() != length) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    memcpy(data, metadata_[type].data(), length);
    *actual = length;
    return ZX_OK;
  }
  zx_status_t DeviceGetMetadataSize(zx_device_t* dev, uint32_t type, size_t* out_size) override {
    if (metadata_.find(type) == metadata_.end()) {
      return ZX_ERR_NOT_FOUND;
    }
    *out_size = metadata_[type].size();
    return ZX_OK;
  }
  zx_status_t DeviceAddMetadata(zx_device_t* dev, uint32_t type, const void* data,
                                size_t length) override {
    std::vector<uint8_t> meta;
    meta.resize(length);
    memcpy(meta.data(), data, length);
    dev->metadata[type] = std::move(meta);
    return ZX_OK;
  }
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override {
    if (device->proto_id != proto_id) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(protocol, device->proto_ops, 16);
    return ZX_OK;
  }
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    auto dev = std::make_shared<zx_device>();
    dev->ctx = args->ctx;
    dev->proto_ops = args->proto_ops;
    if (args->props) {
      dev->props.resize(args->prop_count);
      memcpy(dev->props.data(), args->props, args->prop_count * sizeof(zx_device_prop_t));
    }
    dev->dev_ops = *args->ops;
    dev->parent = parent->weak_from_this();
    dev->proto_id = args->proto_id;
    parent->devices.push_back(dev);
    *out = dev.get();
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) override {
    DestroyDevices(device);
    return ZX_OK;
  }

 private:
  std::map<uint32_t, std::vector<uint8_t>> metadata_;
};

class UsbPeripheralHarness : public zxtest::Test {
 public:
  void SetUp() override {
    dci_ = std::make_unique<FakeDevice>();
    root_device_ = std::make_shared<zx_device_t>();
    root_device_->proto_ops = dci_->proto();
    root_device_->ctx = dci_.get();
    root_device_->proto_id = ZX_PROTOCOL_USB_DCI;
    zx::interrupt irq;
    ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));
    ASSERT_OK(usb_peripheral::UsbPeripheral::Create(nullptr, root_device_.get()));
    auto dev = root_device_->devices.front();
    client_ = ddk::UsbDciInterfaceProtocolClient(dci_->interface());
  }

  void TearDown() override {}

 protected:
  std::unique_ptr<FakeDevice> dci_;
  std::shared_ptr<zx_device_t> root_device_;
  Ddk ddk_;
  ddk::UsbDciInterfaceProtocolClient client_;
};

TEST_F(UsbPeripheralHarness, AddsCorrectSerialNumberMetadata) {
  char serial[256];
  usb_setup_t setup;
  setup.wLength = sizeof(serial);
  setup.wValue = 0x3 | (USB_DT_STRING << 8);
  setup.bmRequestType = USB_DIR_IN | USB_RECIP_DEVICE | USB_TYPE_STANDARD;
  setup.bRequest = USB_REQ_GET_DESCRIPTOR;
  size_t actual;
  ASSERT_OK(client_.Control(&setup, nullptr, 0, &serial, sizeof(serial), &actual));
  ASSERT_EQ(serial[0], sizeof(kSerialNumber) * 2);
  ASSERT_EQ(serial[1], USB_DT_STRING);
  for (size_t i = 0; i < sizeof(kSerialNumber) - 1; i++) {
    ASSERT_EQ(serial[2 + (i * 2)], kSerialNumber[i]);
  }
  DestroyDevices(root_device_.get());
}

TEST_F(UsbPeripheralHarness, WorksWithVendorSpecificCommandWhenConfigurationIsZero) {
  char serial[256];
  usb_setup_t setup;
  setup.wLength = sizeof(serial);
  setup.wValue = 0x3 | (USB_DT_STRING << 8);
  setup.bmRequestType = USB_DIR_IN | USB_RECIP_DEVICE | USB_TYPE_VENDOR;
  setup.bRequest = USB_REQ_GET_DESCRIPTOR;
  size_t actual;
  ASSERT_EQ(client_.Control(&setup, nullptr, 0, &serial, sizeof(serial), &actual),
            ZX_ERR_BAD_STATE);
  DestroyDevices(root_device_.get());
}
