// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "phy-device.h"

#include <fuchsia/wlan/device/llcpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <stdio.h>

#include <algorithm>

#include <ddk/debug.h>
#include <ddk/protocol/wlanphy.h>
#include <wlan/common/element.h>
#include <wlan/common/phy.h>

#include "ddktl/fidl.h"
#include "driver.h"
#include "iface-device.h"

namespace wlan {
namespace testing {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;
namespace wlan_mlme = ::fuchsia::wlan::mlme;

#define DEV(c) static_cast<PhyDevice*>(c)
static zx_protocol_device_t wlanphy_test_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_msg_t* msg,
                  fidl_txn_t* txn) { return DEV(ctx)->Message(msg, txn); },
};
#undef DEV

static wlanphy_protocol_ops_t wlanphy_test_ops = {
    .dummy = nullptr,
};

class DeviceConnector : public llcpp::fuchsia::wlan::device::Connector::Interface {
 public:
  DeviceConnector(PhyDevice* device) : device_(device) {}
  void Connect(::zx::channel request, ConnectCompleter::Sync& _completer) override {
    device_->Connect(std::move(request));
  }

 private:
  PhyDevice* device_;
};

PhyDevice::PhyDevice(zx_device_t* device) : parent_(device) {}

zx_status_t PhyDevice::Bind() {
  zxlogf(INFO, "wlan::testing::phy::PhyDevice::Bind()");

  dispatcher_ = std::make_unique<wlan::common::Dispatcher<wlan_device::Phy>>(wlanphy_async_t());

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlanphy-test";
  args.ctx = this;
  args.ops = &wlanphy_test_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANPHY;
  args.proto_ops = &wlanphy_test_ops;

  zx_status_t status = device_add(parent_, &args, &zxdev_);
  if (status != ZX_OK) {
    printf("wlanphy-test: could not add test device: %d\n", status);
  }

  return status;
}

void PhyDevice::Unbind() {
  zxlogf(INFO, "wlan::testing::PhyDevice::Unbind()");
  std::lock_guard<std::mutex> guard(lock_);
  dispatcher_.reset();
  device_unbind_reply(zxdev_);
}

void PhyDevice::Release() {
  zxlogf(INFO, "wlan::testing::PhyDevice::Release()");
  delete this;
}

zx_status_t PhyDevice::Message(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  DeviceConnector connector(this);

  llcpp::fuchsia::wlan::device::Connector::Dispatch(&connector, msg, &transaction);
  return transaction.Status();
}

namespace {
wlan_device::PhyInfo get_info() {
  wlan_device::PhyInfo info;

  // The "local" bit is set to prevent collisions with globally-administered MAC addresses.
  static const uint8_t kTestMacAddr[] = {
      0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
  };
  memcpy(info.hw_mac_address.data(), kTestMacAddr, info.hw_mac_address.size());

  info.supported_phys.resize(0);
  info.driver_features.resize(0);
  info.mac_roles.resize(0);
  info.caps.resize(0);
  info.bands.resize(0);

  info.supported_phys.push_back(wlan_device::SupportedPhy::DSSS);
  info.supported_phys.push_back(wlan_device::SupportedPhy::CCK);
  info.supported_phys.push_back(wlan_device::SupportedPhy::OFDM);
  info.supported_phys.push_back(wlan_device::SupportedPhy::HT);

  info.driver_features.push_back(wlan_common::DriverFeature::SYNTH);

  info.mac_roles.push_back(wlan_device::MacRole::CLIENT);
  info.mac_roles.push_back(wlan_device::MacRole::AP);

  info.caps.push_back(wlan_device::Capability::SHORT_PREAMBLE);
  info.caps.push_back(wlan_device::Capability::SHORT_SLOT_TIME);

  wlan_device::BandInfo band24;
  band24.band_id = wlan_common::Band::WLAN_BAND_2GHZ;

  HtCapabilities ht_caps;
  ht_caps.ht_cap_info.set_val(0x01fe);
  ht_caps.mcs_set.rx_mcs_head.set_val(0x01000000ff);
  ht_caps.mcs_set.rx_mcs_tail.set_val(0);
  ht_caps.mcs_set.tx_mcs.set_val(0x10);

  band24.ht_caps = wlan_mlme::HtCapabilities::New();
  static_assert(sizeof(band24.ht_caps->bytes) == sizeof(ht_caps));
  memcpy(band24.ht_caps->bytes.data(), &ht_caps, sizeof(ht_caps));

  band24.rates = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108};
  band24.supported_channels.base_freq = 2417;
  band24.supported_channels.channels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

  info.bands.push_back(std::move(band24));

  wlan_device::BandInfo band5;
  band5.band_id = wlan_common::Band::WLAN_BAND_5GHZ;

  ht_caps = HtCapabilities{};
  ht_caps.ht_cap_info.set_val(0x01fe);
  ht_caps.mcs_set.rx_mcs_head.set_val(0x010000ffff);
  ht_caps.mcs_set.rx_mcs_tail.set_val(0);
  ht_caps.mcs_set.tx_mcs.set_val(0x10);

  band5.ht_caps = wlan_mlme::HtCapabilities::New();
  static_assert(sizeof(band5.ht_caps->bytes) == sizeof(ht_caps));
  memcpy(band5.ht_caps->bytes.data(), &ht_caps, sizeof(ht_caps));

  band5.rates = {12, 18, 24, 36, 48, 72, 96, 108};
  band5.supported_channels.base_freq = 5000;
  band5.supported_channels.channels = {36,  38,  40,  42,  44,  46,  48,  50,  52,  54,  56,  58,
                                       60,  62,  64,  100, 102, 104, 106, 108, 110, 112, 114, 116,
                                       118, 120, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140,
                                       149, 151, 153, 155, 157, 159, 161, 165, 184, 188, 192, 196};

  info.bands.push_back(std::move(band5));
  return info;
}
}  // namespace

void PhyDevice::Query(QueryCallback callback) {
  zxlogf(INFO, "wlan::testing::phy::PhyDevice::Query()");
  wlan_device::QueryResponse resp;
  resp.info = get_info();
  callback(std::move(resp));
}

void PhyDevice::CreateIface(wlan_device::CreateIfaceRequest req, CreateIfaceCallback callback) {
  zxlogf(INFO, "CreateRequest: role=%u", req.role);
  std::lock_guard<std::mutex> guard(lock_);
  wlan_device::CreateIfaceResponse resp;

  // We leverage wrapping of unsigned ints to cycle back through ids to find an unused one.
  bool found_unused = false;
  uint16_t id = next_id_;
  while (!found_unused) {
    if (ifaces_.count(id) > 0) {
      id++;
      // If we wrap all the way around, something is very wrong.
      if (next_id_ == id) {
        break;
      }
    } else {
      found_unused = true;
    }
  }
  ZX_DEBUG_ASSERT(found_unused);
  if (!found_unused) {
    resp.status = ZX_ERR_NO_RESOURCES;
    callback(std::move(resp));
    return;
  }

  wlan_info_mac_role_t role = 0;
  switch (req.role) {
    case wlan_device::MacRole::CLIENT:
      role = WLAN_INFO_MAC_ROLE_CLIENT;
      break;
    case wlan_device::MacRole::AP:
      role = WLAN_INFO_MAC_ROLE_AP;
      break;
    case wlan_device::MacRole::MESH:
      role = WLAN_INFO_MAC_ROLE_MESH;
      break;
    default:
      resp.status = ZX_ERR_NOT_SUPPORTED;
      callback(std::move(resp));
      return;
  }

  // Create the interface device and bind it.
  auto macdev = std::make_unique<IfaceDevice>(zxdev_, role);
  zx_status_t status = macdev->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not bind child wlanmac device: %d", status);
    resp.status = status;
    callback(std::move(resp));
    return;
  }

  // Memory management follows the device lifecycle at this point. The only way an interface can be
  // removed is through this phy device, either through a DestroyIface call or by the phy going
  // away, so it should be safe to store the raw pointer.
  ifaces_[id] = macdev.release();

  // Since we successfully used the id, increment the next id counter.
  next_id_ = id + 1;

  resp.iface_id = id;
  resp.status = ZX_OK;
  callback(std::move(resp));
}

void PhyDevice::DestroyIface(wlan_device::DestroyIfaceRequest req, DestroyIfaceCallback callback) {
  zxlogf(INFO, "DestroyRequest: id=%u", req.id);

  wlan_device::DestroyIfaceResponse resp;

  std::lock_guard<std::mutex> guard(lock_);
  auto intf = ifaces_.find(req.id);
  if (intf == ifaces_.end()) {
    resp.status = ZX_ERR_NOT_FOUND;
    callback(std::move(resp));
    return;
  }

  device_async_remove(intf->second->zxdev());
  // Remove the device from our map. We do NOT free the memory, since the devhost owns it and will
  // call release when it's safe to free the memory.
  ifaces_.erase(req.id);

  resp.status = ZX_OK;
  callback(std::move(resp));
}

void PhyDevice::SetCountry(wlan_device::CountryCode req, SetCountryCallback callback) {
  zxlogf(INFO, "testing/PHY: SetCountry [%s]", wlan::common::Alpha2ToStr(req.alpha2).c_str());
  callback(ZX_OK);
}

void PhyDevice::GetCountry(GetCountryCallback callback) {
  zxlogf(INFO, "testing/PHY: GetCountry");
  callback(fit::error(ZX_ERR_NOT_SUPPORTED));
}

void PhyDevice::ClearCountry(ClearCountryCallback callback) {
  zxlogf(INFO, "testing/PHY: ClearCountry");
  callback(ZX_OK);
}

zx_status_t PhyDevice::Connect(zx::channel request) {
  return dispatcher_->AddBinding(std::move(request), this);
}

}  // namespace testing
}  // namespace wlan
