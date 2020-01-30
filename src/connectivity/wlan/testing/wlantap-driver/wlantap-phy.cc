// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wlantap-phy.h"

#include <fuchsia/wlan/device/cpp/fidl.h>
#include <zircon/status.h>

#include <array>

#include <ddk/debug.h>
#include <ddk/protocol/wlanphyimpl.h>
#include <wlan/common/dispatcher.h>
#include <wlan/common/phy.h>
#include <wlan/protocol/mac.h>

#include "utils.h"
#include "wlantap-mac.h"

namespace wlan {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;
namespace wlantap = ::fuchsia::wlan::tap;

namespace {

template <typename T, size_t N>
::std::array<T, N> ToFidlArray(const T (&c_array)[N]) {
  ::std::array<T, N> ret;
  std::copy_n(&c_array[0], N, ret.begin());
  return ret;
}

template <typename T, size_t MAX_COUNT>
class DevicePool {
 public:
  template <class F>
  zx_status_t TryCreateNew(F factory, uint16_t* out_id) {
    for (size_t id = 0; id < MAX_COUNT; ++id) {
      if (pool_[id] == nullptr) {
        T* dev = nullptr;
        zx_status_t status = factory(id, &dev);
        if (status != ZX_OK) {
          return status;
        }
        pool_[id] = dev;
        *out_id = id;
        return ZX_OK;
      }
    }
    return ZX_ERR_NO_RESOURCES;
  }

  T* Get(uint16_t id) {
    if (id >= MAX_COUNT) {
      return nullptr;
    }
    return pool_[id];
  }

  T* Release(uint16_t id) {
    if (id >= MAX_COUNT) {
      return nullptr;
    }
    T* ret = pool_[id];
    pool_[id] = nullptr;
    return ret;
  }

  void ReleaseAll() { std::fill(pool_.begin(), pool_.end(), nullptr); }

 private:
  std::array<T*, MAX_COUNT> pool_{};
};

constexpr size_t kMaxMacDevices = 4;

wlantap::SetKeyArgs ToSetKeyArgs(uint16_t wlanmac_id, const wlan_key_config_t* config) {
  auto set_key_args = wlantap::SetKeyArgs{
      .wlanmac_id = wlanmac_id,
      .config =
          wlantap::WlanKeyConfig{
              .protection = config->protection,
              .cipher_oui = ToFidlArray(config->cipher_oui),
              .cipher_type = config->cipher_type,
              .key_type = config->key_type,
              .peer_addr = ToFidlArray(config->peer_addr),
              .key_idx = config->key_idx,
          },
  };
  auto& key = set_key_args.config.key;
  key.clear();
  key.reserve(config->key_len);
  std::copy_n(config->key, config->key_len, std::back_inserter(key));
  return set_key_args;
}

wlantap::TxArgs ToTxArgs(uint16_t wlanmac_id, const wlan_tx_packet_t* pkt) {
  auto tx_args = wlantap::TxArgs{
      .wlanmac_id = wlanmac_id,
      .packet = wlantap::WlanTxPacket{.info =
                                          wlantap::WlanTxInfo{
                                              .tx_flags = pkt->info.tx_flags,
                                              .valid_fields = pkt->info.valid_fields,
                                              .phy = pkt->info.phy,
                                              .cbw = pkt->info.cbw,
                                              .mcs = pkt->info.mcs,
                                              .tx_vector_idx = pkt->info.tx_vector_idx,
                                          }},
  };
  auto& data = tx_args.packet.data;
  data.clear();
  auto head = static_cast<const uint8_t*>(pkt->packet_head.data_buffer);
  std::copy_n(head, pkt->packet_head.data_size, std::back_inserter(data));
  if (pkt->packet_tail_list != nullptr) {
    auto tail = static_cast<const uint8_t*>(pkt->packet_tail_list->data_buffer);
    std::copy_n(tail + pkt->tail_offset, pkt->packet_tail_list->data_size - pkt->tail_offset,
                std::back_inserter(data));
  }
  return tx_args;
}

struct WlantapPhy : wlantap::WlantapPhy, WlantapMac::Listener {
  WlantapPhy(zx_device_t* device, zx::channel user_channel,
             std::unique_ptr<wlantap::WlantapPhyConfig> phy_config, async_dispatcher_t* loop)
      : phy_config_(std::move(phy_config)),
        loop_(loop),
        user_binding_(this, std::move(user_channel), loop),
        name_("wlantap phy " + phy_config_->name) {
    user_binding_.set_error_handler([this](zx_status_t status) {
      auto name = name_;
      zxlogf(INFO, "%s: unbinding device because the channel was closed\n", name.c_str());
      if (report_tx_status_count_) {
        zxlogf(INFO, "Tx Status Reports sent druing device lifetime: %zu\n",
               report_tx_status_count_);
      }
      // Remove the device if the client closed the channel
      Unbind();
      zxlogf(INFO, "%s: done unbinding\n", name.c_str());
    });
  }

  static void DdkUnbind(void* ctx) {
    auto self = static_cast<WlantapPhy*>(ctx);
    auto name = self->name_;
    zxlogf(INFO, "%s: unbinding device per request from DDK\n", name.c_str());
    if (self->report_tx_status_count_) {
      zxlogf(INFO, "Tx Status Reports sent druing device lifetime: %zu\n",
             self->report_tx_status_count_);
    }
    self->Unbind();
    zxlogf(INFO, "%s: done unbinding\n", name.c_str());
  }

  static void DdkRelease(void* ctx) {
    auto name = static_cast<WlantapPhy*>(ctx)->name_;
    zxlogf(INFO, "%s: DdkRelease\n", name.c_str());
    delete static_cast<WlantapPhy*>(ctx);
    zxlogf(INFO, "%s: DdkRelease done\n", name.c_str());
  }

  void Unbind() {
    std::lock_guard<std::mutex> guard(lock_);
    if (user_binding_.is_bound()) {
      user_binding_.Unbind();
    }
    user_binding_.set_error_handler(nullptr);

    // Flush any remaining tasks in the event loop before destroying the
    // interfaces
    ::async::PostTask(loop_, [this] {
      {
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        wlanmac_devices_.ReleaseAll();
      }
      device_async_remove(device_);
    });
  }

  // wlanphy-impl DDK interface

  zx_status_t Query(wlanphy_impl_info_t* info) {
    zxlogf(INFO, "%s: received a 'Query' DDK request\n", name_.c_str());
    zx_status_t status = ConvertPhyInfo(&info->wlan_info, phy_config_->phy_info);
    zxlogf(INFO, "%s: responded to 'Query' with status %s\n", name_.c_str(),
           zx_status_get_string(status));
    return status;
  }

  template <typename V, typename T>
  static bool contains(const V& v, const T& t) {
    return std::find(v.cbegin(), v.cend(), t) != v.cend();
  }

  static std::string RoleToString(wlan_device::MacRole role) {
    switch (role) {
      case wlan_device::MacRole::CLIENT:
        return "client";
      case wlan_device::MacRole::AP:
        return "ap";
      case wlan_device::MacRole::MESH:
        return "mesh";
      default:
        return "invalid";
    }
  }

  zx_status_t CreateIface(const wlanphy_impl_create_iface_req_t* req, uint16_t* out_iface_id) {
    zxlogf(INFO, "%s: received a 'CreateIface' DDK request\n", name_.c_str());
    wlan_device::MacRole dev_role = ConvertMacRole(req->role);
    auto role_str = RoleToString(dev_role);
    if (!contains(phy_config_->phy_info.mac_roles, dev_role)) {
      zxlogf(ERROR, "%s: CreateIface(%s): role not supported\n", name_.c_str(), role_str.c_str());
      return ZX_ERR_NOT_SUPPORTED;
    }
    if (!contains(phy_config_->phy_info.driver_features,
                  wlan_common::DriverFeature::TEMP_DIRECT_SME_CHANNEL)) {
      zxlogf(ERROR, "%s: CraeteIface(%s): phy without SME channel support is not allowed.\n",
             name_.c_str(), role_str.c_str());
      ZX_ASSERT(0);
      return ZX_ERR_NOT_SUPPORTED;
    }

    zx::channel sme_channel = zx::channel(req->sme_channel);
    if (!sme_channel.is_valid()) {
      return ZX_ERR_IO_INVALID;
    }
    std::lock_guard<std::mutex> guard(wlanmac_lock_);
    zx_status_t status = wlanmac_devices_.TryCreateNew(
        [&](uint16_t id, WlantapMac** out_dev) {
          return CreateWlantapMac(device_, dev_role, phy_config_.get(), id, this,
                                  std::move(sme_channel), out_dev);
        },
        out_iface_id);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: CreateIface(%s): maximum number of interfaces already reached\n",
             name_.c_str(), role_str.c_str());
      return ZX_ERR_NO_RESOURCES;
    }
    zxlogf(INFO, "%s: CreateIface(%s): success\n", name_.c_str(), role_str.c_str());
    return ZX_OK;
  }

  zx_status_t DestroyIface(uint16_t id) {
    zxlogf(INFO, "%s: received a 'DestroyIface' DDK request\n", name_.c_str());
    std::lock_guard<std::mutex> guard(wlanmac_lock_);
    WlantapMac* wlanmac = wlanmac_devices_.Release(id);
    if (wlanmac == nullptr) {
      zxlogf(ERROR, "%s: DestroyIface: invalid iface id\n", name_.c_str());
      return ZX_ERR_INVALID_ARGS;
    }
    wlanmac->RemoveDevice();
    zxlogf(ERROR, "%s: DestroyIface: done\n", name_.c_str());
    return ZX_OK;
  }

  zx_status_t SetCountry(const wlanphy_country_t* country) {
    if (country == nullptr) {
      zxlogf(ERROR, "%s: SetCountry() received nullptr\n", name_.c_str());
      return ZX_ERR_INVALID_ARGS;
    }
    zxlogf(INFO, "%s: SetCountry() to [%s]\n", name_.c_str(),
           wlan::common::Alpha2ToStr(country->alpha2).c_str());
    std::lock_guard<std::mutex> guard(lock_);
    if (!user_binding_.is_bound()) {
      zxlogf(ERROR, "%s: SetCountry() failed: user_binding not bound\n", name_.c_str());
      return ZX_ERR_BAD_STATE;
    }

    zxlogf(INFO, "%s: SetCountry() to [%s] received\n", name_.c_str(),
           wlan::common::Alpha2ToStr(country->alpha2).c_str());

    auto args = wlantap::SetCountryArgs{};
    memcpy(&args.alpha2, country->alpha2, WLANPHY_ALPHA2_LEN);
    user_binding_.events().SetCountry(args);

    return ZX_OK;
  }

  // wlantap::WlantapPhy impl

  virtual void Rx(uint16_t wlanmac_id, ::std::vector<uint8_t> data,
                  wlantap::WlanRxInfo info) override {
    zxlogf(INFO, "%s: Rx(%zu bytes)\n", name_.c_str(), data.size());
    std::lock_guard<std::mutex> guard(wlanmac_lock_);
    if (WlantapMac* wlanmac = wlanmac_devices_.Get(wlanmac_id)) {
      wlanmac->Rx(data, info);
    }
    zxlogf(INFO, "%s: Rx done\n", name_.c_str());
  }

  virtual void Status(uint16_t wlanmac_id, uint32_t st) override {
    zxlogf(INFO, "%s: Status(%u)\n", name_.c_str(), st);
    std::lock_guard<std::mutex> guard(wlanmac_lock_);
    if (WlantapMac* wlanmac = wlanmac_devices_.Get(wlanmac_id)) {
      wlanmac->Status(st);
    }
    zxlogf(INFO, "%s: Status done\n", name_.c_str());
  }

  virtual void ReportTxStatus(uint16_t wlanmac_id, wlantap::WlanTxStatus ts) override {
    std::lock_guard<std::mutex> guard(wlanmac_lock_);
    if (!phy_config_->quiet || report_tx_status_count_ < 32) {
      zxlogf(INFO, "%s: ReportTxStatus %zu\n", name_.c_str(), report_tx_status_count_);
    }
    if (WlantapMac* wlanmac = wlanmac_devices_.Get(wlanmac_id)) {
      ++report_tx_status_count_;
      wlanmac->ReportTxStatus(ts);
    }
    if (!phy_config_->quiet || report_tx_status_count_ <= 32) {
      zxlogf(INFO, "%s: ReportTxStatus %zu done\n", name_.c_str(), report_tx_status_count_);
    }
  }

  // WlantapMac::Listener impl

  virtual void WlantapMacStart(uint16_t wlanmac_id) override {
    zxlogf(INFO, "%s: WlantapMacStart id=%u\n", name_.c_str(), wlanmac_id);
    std::lock_guard<std::mutex> guard(lock_);
    if (!user_binding_.is_bound()) {
      return;
    }
    user_binding_.events().WlanmacStart({.wlanmac_id = wlanmac_id});
    zxlogf(INFO, "%s: WlantapMacStart done\n", name_.c_str());
  }

  virtual void WlantapMacStop(uint16_t wlanmac_id) override {
    zxlogf(INFO, "%s: WlantapMacStop\n", name_.c_str());
  }

  virtual void WlantapMacQueueTx(uint16_t wlanmac_id, wlan_tx_packet_t* pkt) override {
    if (!phy_config_->quiet || report_tx_status_count_ < 32) {
      zxlogf(INFO, "%s: WlantapMacQueueTx id=%u, tx_report_count=%zu\n", name_.c_str(), wlanmac_id,
             report_tx_status_count_);
    }
    std::lock_guard<std::mutex> guard(lock_);
    if (!user_binding_.is_bound()) {
      return;
    }
    size_t pkt_size = pkt->packet_head.data_size;
    if (pkt->packet_tail_list != nullptr) {
      pkt_size += pkt->packet_tail_list->data_size - pkt->tail_offset;
    }
    user_binding_.events().Tx(ToTxArgs(wlanmac_id, pkt));
    if (!phy_config_->quiet || report_tx_status_count_ < 32) {
      zxlogf(INFO, "%s: WlantapMacQueueTx done(%zu bytes), tx_report_count=%zu\n", name_.c_str(),
             pkt_size, report_tx_status_count_);
    }
  }

  virtual void WlantapMacSetChannel(uint16_t wlanmac_id, const wlan_channel_t* channel) override {
    if (!phy_config_->quiet) {
      zxlogf(INFO, "%s: WlantapMacSetChannel id=%u\n", name_.c_str(), wlanmac_id);
    }
    std::lock_guard<std::mutex> guard(lock_);
    if (!user_binding_.is_bound()) {
      return;
    }
    user_binding_.events().SetChannel({.wlanmac_id = wlanmac_id,
                                       .chan = {.primary = channel->primary,
                                                .cbw = static_cast<wlan_common::CBW>(channel->cbw),
                                                .secondary80 = channel->secondary80}});
    if (!phy_config_->quiet) {
      zxlogf(INFO, "%s: WlantapMacSetChannel done\n", name_.c_str());
    }
  }

  virtual void WlantapMacConfigureBss(uint16_t wlanmac_id,
                                      const wlan_bss_config_t* config) override {
    zxlogf(INFO, "%s: WlantapMacConfigureBss id=%u\n", name_.c_str(), wlanmac_id);
    std::lock_guard<std::mutex> guard(lock_);
    if (!user_binding_.is_bound()) {
      return;
    }
    user_binding_.events().ConfigureBss({.wlanmac_id = wlanmac_id,
                                         .config = {.bss_type = config->bss_type,
                                                    .bssid = ToFidlArray(config->bssid),
                                                    .remote = config->remote}});
    zxlogf(INFO, "%s: WlantapMacConfigureBss done\n", name_.c_str());
  }

  virtual void WlantapMacSetKey(uint16_t wlanmac_id, const wlan_key_config_t* key_config) override {
    zxlogf(INFO, "%s: WlantapMacSetKey id=%u\n", name_.c_str(), wlanmac_id);
    std::lock_guard<std::mutex> guard(lock_);
    if (!user_binding_.is_bound()) {
      return;
    }
    user_binding_.events().SetKey(ToSetKeyArgs(wlanmac_id, key_config));
    zxlogf(INFO, "%s: WlantapMacSetKey done\n", name_.c_str());
  }

  zx_device_t* device_;
  const std::unique_ptr<const wlantap::WlantapPhyConfig> phy_config_;
  async_dispatcher_t* loop_;
  std::mutex wlanmac_lock_;
  DevicePool<WlantapMac, kMaxMacDevices> wlanmac_devices_ __TA_GUARDED(wlanmac_lock_);
  std::mutex lock_;
  fidl::Binding<wlantap::WlantapPhy> user_binding_ __TA_GUARDED(lock_);
  size_t report_tx_status_count_ = 0;
  std::string name_;
};

}  // namespace

#define DEV(c) static_cast<WlantapPhy*>(c)
static wlanphy_impl_protocol_ops_t wlanphy_impl_ops = {
    .query = [](void* ctx, wlanphy_impl_info_t* info) -> zx_status_t {
      return DEV(ctx)->Query(info);
    },
    .create_iface = [](void* ctx, const wlanphy_impl_create_iface_req_t* req,
                       uint16_t* out_iface_id) -> zx_status_t {
      return DEV(ctx)->CreateIface(req, out_iface_id);
    },
    .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t {
      return DEV(ctx)->DestroyIface(id);
    },
    .set_country = [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
      return DEV(ctx)->SetCountry(country);
    },
};
#undef DEV

zx_status_t CreatePhy(zx_device_t* wlantapctl, zx::channel user_channel,
                      std::unique_ptr<wlantap::WlantapPhyConfig> phy_config_from_fidl,
                      async_dispatcher_t* loop) {
  zxlogf(INFO, "wlantap: creating phy\n");
  auto phy = std::make_unique<WlantapPhy>(wlantapctl, std::move(user_channel),
                                          std::move(phy_config_from_fidl), loop);
  static zx_protocol_device_t device_ops = {.version = DEVICE_OPS_VERSION,
                                            .unbind = &WlantapPhy::DdkUnbind,
                                            .release = &WlantapPhy::DdkRelease};
  device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                            .name = phy->phy_config_->name.c_str(),
                            .ctx = phy.get(),
                            .ops = &device_ops,
                            .proto_id = ZX_PROTOCOL_WLANPHY_IMPL,
                            .proto_ops = &wlanphy_impl_ops};
  zx_status_t status = device_add(wlantapctl, &args, &phy->device_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "wlantap: %s: could not add device: %d\n", __func__, status);
    return status;
  }
  // Transfer ownership to devmgr
  phy.release();
  zxlogf(INFO, "wlantap: phy successfully created\n");
  return ZX_OK;
}

}  // namespace wlan
