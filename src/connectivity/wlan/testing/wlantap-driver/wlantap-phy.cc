// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wlantap-phy.h"

#include <fidl/fuchsia.wlan.softmac/cpp/driver/wire.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <array>
#include <chrono>
#include <mutex>

#include <wlan/common/dispatcher.h>
#include <wlan/common/phy.h>

#include "src/connectivity/wlan/drivers/wlansoftmac/convert.h"
#include "utils.h"
#include "wlantap-mac.h"

namespace wlan {

namespace wlan_softmac = fuchsia_wlan_softmac::wire;
namespace wlan_common = fuchsia_wlan_common::wire;

namespace {

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

wlan_tap::SetKeyArgs ToSetKeyArgs(uint16_t wlan_softmac_id,
                                  const wlan_softmac::WlanKeyConfig& config) {
  ZX_ASSERT(config.has_protection() && config.has_cipher_oui() && config.has_cipher_type() &&
            config.has_key_type() && config.has_peer_addr() && config.has_key_idx() &&
            config.has_key());

  auto set_key_args = wlan_tap::SetKeyArgs{
      .wlan_softmac_id = wlan_softmac_id,
      .config =
          wlan_tap::WlanKeyConfig{
              .protection = static_cast<uint8_t>(config.protection()),
              .cipher_oui = config.cipher_oui(),
              .cipher_type = config.cipher_type(),
              .key_type = static_cast<uint8_t>(config.key_type()),
              .peer_addr = config.peer_addr(),
              .key_idx = config.key_idx(),
          },
  };
  set_key_args.config.key = fidl::VectorView<uint8_t>::FromExternal(
      const_cast<uint8_t*>(config.key().begin()), config.key().count());
  return set_key_args;
}

wlan_tap::TxArgs ToTxArgs(uint16_t wlan_softmac_id, const wlan_softmac::WlanTxPacket pkt) {
  if (pkt.info.phy < wlan_common::WlanPhyType::kDsss ||
      pkt.info.phy > wlan_common::WlanPhyType::kHe) {
    ZX_PANIC("Unknown PHY in wlan_tx_packet_t: %u.", static_cast<uint8_t>(pkt.info.phy));
  }
  wlan_tap::WlanTxInfo tap_info = {
      .tx_flags = pkt.info.tx_flags,
      .valid_fields = pkt.info.valid_fields,
      .tx_vector_idx = pkt.info.tx_vector_idx,
      .phy = pkt.info.phy,
      .cbw = static_cast<uint8_t>(pkt.info.channel_bandwidth),
      .mcs = pkt.info.mcs,
  };
  auto tx_args = wlan_tap::TxArgs{
      .wlan_softmac_id = wlan_softmac_id,
      .packet = wlan_tap::WlanTxPacket{.data = pkt.mac_frame, .info = tap_info},
  };

  return tx_args;
}

struct WlantapPhy : public fidl::WireServer<fuchsia_wlan_tap::WlantapPhy>, WlantapMac::Listener {
  WlantapPhy(zx_device_t* device, zx::channel user_channel,
             std::shared_ptr<wlan_tap::WlantapPhyConfig> phy_config, async_dispatcher_t* loop)
      : phy_config_(phy_config),
        loop_(loop),
        name_("wlan_tap phy " + std::string(phy_config_->name.get())),
        user_binding_(fidl::BindServer(
            loop_, fidl::ServerEnd<fuchsia_wlan_tap::WlantapPhy>(std::move(user_channel)), this,
            [this](WlantapPhy* server_impl, fidl::UnbindInfo info,
                   fidl::ServerEnd<fuchsia_wlan_tap::WlantapPhy> server_end) {
              auto name = name_;
              fidl_server_unbound_ = true;

              if (shutdown_called_) {
                zxlogf(INFO, "%s: Unbinding WlantapPhy FIDL server.", name.c_str());
              } else {
                zxlogf(ERROR, "%s: Unbinding WlantapPhy FIDL server before Shutdown() called. %s",
                       name.c_str(), info.FormatDescription().c_str());
              }

              if (report_tx_status_count_) {
                zxlogf(INFO, "Tx Status Reports sent during device lifetime: %zu",
                       report_tx_status_count_);
              }

              zxlogf(INFO, "%s: Removing PHY device asynchronously.", name.c_str());
              device_async_remove(device_);

              zxlogf(INFO, "%s: WlantapPhy FIDL server unbind complete.", name.c_str());
            })) {}

  static void DdkUnbind(void* ctx) {
    auto self = static_cast<WlantapPhy*>(ctx);
    auto name = self->name_;
    zxlogf(INFO, "%s: Unbinding PHY device.", name.c_str());

    // This call will be ignored by ServerBindingRef it is has
    // already been called, i.e., in the case that DdkUnbind precedes
    // normal shutdowns.
    std::lock_guard<std::mutex> guard(self->fidl_server_lock_);
    self->user_binding_.Unbind();

    zxlogf(INFO, "%s: PHY device unbind complete.", name.c_str());
  }

  static void DdkRelease(void* ctx) {
    auto self = static_cast<WlantapPhy*>(ctx);
    auto name = self->name_;
    zxlogf(INFO, "%s: DdkRelease", name.c_str());

    // Flush any remaining tasks in the event loop before destroying the iface.
    // Placed in a block to avoid m, lk, and cv from unintentionally escaping
    // their specific use here.
    {
      std::mutex m;
      std::unique_lock lk(m);
      std::condition_variable cv;
      ::async::PostTask(self->loop_, [&lk, &cv]() mutable {
        lk.unlock();
        cv.notify_one();
      });
      auto status = cv.wait_for(lk, self->kFidlServerShutdownTimeout);
      if (status == std::cv_status::timeout) {
        zxlogf(ERROR, "%s: timed out waiting for FIDL server dispatcher to complete.",
               name.c_str());
        zxlogf(WARNING, "%s: Deleting wlansoftmac devices while FIDL server dispatcher running.",
               name.c_str());
      }
    }

    std::lock_guard<std::mutex> guard(self->wlan_softmac_lock_);
    self->wlan_softmac_devices_.ReleaseAll();

    delete self;
    zxlogf(INFO, "%s: DdkRelease done", name.c_str());
  }

  // wlanphy-impl DDK interface

  zx_status_t GetSupportedMacRoles(
      wlan_mac_role_t out_supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
      uint8_t* out_supported_mac_roles_count) {
    zxlogf(INFO, "%s: received a 'GetSupportedMacRoles' DDK request", name_.c_str());
    zx_status_t status = ConvertTapPhyConfig(out_supported_mac_roles_list,
                                             out_supported_mac_roles_count, *phy_config_);
    zxlogf(INFO, "%s: responded to 'GetSupportedMacRoles' with status %s", name_.c_str(),
           zx_status_get_string(status));
    return status;
  }

  template <typename V, typename T>
  static bool contains(const V& v, const T& t) {
    return std::find(v.cbegin(), v.cend(), t) != v.cend();
  }

  static std::string RoleToString(wlan_common::WlanMacRole role) {
    switch (role) {
      case wlan_common::WlanMacRole::kClient:
        return "client";
      case wlan_common::WlanMacRole::kAp:
        return "ap";
      case wlan_common::WlanMacRole::kMesh:
        return "mesh";
      default:
        return "invalid";
    }
  }

  zx_status_t CreateIface(const wlanphy_impl_create_iface_req_t* req, uint16_t* out_iface_id) {
    zxlogf(INFO, "%s: received a 'CreateIface' DDK request", name_.c_str());
    wlan_common::WlanMacRole dev_role;
    zx_status_t status = ConvertMacRole(req->role, &dev_role);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: ConvertMacRole failed: %s", name_.c_str(), zx_status_get_string(status));
      return status;
    }
    auto role_str = RoleToString(dev_role);
    if (phy_config_->mac_role != dev_role) {
      zxlogf(ERROR, "%s: CreateIface(%s): role not supported", name_.c_str(), role_str.c_str());
      return ZX_ERR_NOT_SUPPORTED;
    }

    zx::channel mlme_channel = zx::channel(req->mlme_channel);
    if (!mlme_channel.is_valid()) {
      return ZX_ERR_IO_INVALID;
    }
    std::lock_guard<std::mutex> guard(wlan_softmac_lock_);
    status = wlan_softmac_devices_.TryCreateNew(
        [&](uint16_t id, WlantapMac** out_dev) {
          return CreateWlantapMac(device_, dev_role, phy_config_, id, this, std::move(mlme_channel),
                                  out_dev);
        },
        out_iface_id);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: CreateIface(%s): maximum number of interfaces already reached",
             name_.c_str(), role_str.c_str());
      return ZX_ERR_NO_RESOURCES;
    }
    zxlogf(INFO, "%s: CreateIface(%s): success", name_.c_str(), role_str.c_str());
    return ZX_OK;
  }

  zx_status_t DestroyIface(uint16_t id) {
    zxlogf(INFO, "%s: received a 'DestroyIface' DDK request", name_.c_str());
    std::lock_guard<std::mutex> guard(wlan_softmac_lock_);
    WlantapMac* wlan_softmac = wlan_softmac_devices_.Release(id);
    if (wlan_softmac == nullptr) {
      zxlogf(ERROR, "%s: DestroyIface: invalid iface id", name_.c_str());
      return ZX_ERR_INVALID_ARGS;
    }
    wlan_softmac->RemoveDevice();
    zxlogf(DEBUG, "%s: DestroyIface: done", name_.c_str());
    return ZX_OK;
  }

  zx_status_t SetCountry(const wlanphy_country_t* country) {
    if (country == nullptr) {
      zxlogf(ERROR, "%s: SetCountry() received nullptr", name_.c_str());
      return ZX_ERR_INVALID_ARGS;
    }
    zxlogf(INFO, "%s: SetCountry() to [%s]", name_.c_str(),
           wlan::common::Alpha2ToStr(country->alpha2).c_str());
    std::lock_guard<std::mutex> guard(fidl_server_lock_);

    zxlogf(INFO, "%s: SetCountry() to [%s] received", name_.c_str(),
           wlan::common::Alpha2ToStr(country->alpha2).c_str());

    auto args = wlan_tap::SetCountryArgs{};
    memcpy(&args.alpha2, country->alpha2, WLANPHY_ALPHA2_LEN);
    fidl::Status status = fidl::WireSendEvent(user_binding_)->SetCountry(args);
    if (!status.ok()) {
      zxlogf(ERROR, "%s: SetCountry() failed: user_binding not bound", status.status_string());
      return status.status();
    }
    return ZX_OK;
  }

  zx_status_t GetCountry(wlanphy_country_t* out_country) {
    if (out_country == nullptr) {
      zxlogf(ERROR, "%s: GetCountry() received nullptr", name_.c_str());
      return ZX_ERR_INVALID_ARGS;
    }
    zxlogf(ERROR, "GetCountry not implemented");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SetPsMode(const wlanphy_ps_mode_t* ps_mode) {
    zxlogf(ERROR, "SetPsMode not implemented");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // wlan_tap::WlantapPhy impl

  void Shutdown(ShutdownCompleter::Sync& completer) override {
    zxlogf(INFO, "%s: Shutdown", name_.c_str());
    std::lock_guard<std::mutex> guard(fidl_server_lock_);

    if (shutdown_called_) {
      zxlogf(WARNING, "%s: PHY device shutdown already initiated.", name_.c_str());
      completer.Reply();
      return;
    }
    shutdown_called_ = true;

    zxlogf(INFO, "%s: PHY device shutdown initiated.", name_.c_str());
    user_binding_.Unbind();
    completer.Reply();
  }

  void Rx(RxRequestView request, RxCompleter::Sync& completer) override {
    zxlogf(INFO, "%s: Rx(%zu bytes)", name_.c_str(), request->data.count());
    std::lock_guard<std::mutex> guard(wlan_softmac_lock_);
    if (WlantapMac* wlan_softmac = wlan_softmac_devices_.Get(request->wlan_softmac_id)) {
      wlan_softmac->Rx(request->data, request->info);
    }
    zxlogf(DEBUG, "%s: Rx done", name_.c_str());
  }

  void Status(StatusRequestView request, StatusCompleter::Sync& completer) override {
    zxlogf(INFO, "%s: Status(%u)", name_.c_str(), request->st);
    std::lock_guard<std::mutex> guard(wlan_softmac_lock_);
    if (WlantapMac* wlan_softmac = wlan_softmac_devices_.Get(request->wlan_softmac_id)) {
      wlan_softmac->Status(request->st);
    }
    zxlogf(DEBUG, "%s: Status done", name_.c_str());
  }

  void ReportTxStatus(ReportTxStatusRequestView request,
                      ReportTxStatusCompleter::Sync& completer) override {
    std::lock_guard<std::mutex> guard(wlan_softmac_lock_);
    if (!phy_config_->quiet || report_tx_status_count_ < 32) {
      zxlogf(INFO, "%s: ReportTxStatus %zu", name_.c_str(), report_tx_status_count_);
    }
    if (WlantapMac* wlan_softmac = wlan_softmac_devices_.Get(request->wlan_softmac_id)) {
      ++report_tx_status_count_;
      wlan_softmac->ReportTxStatus(request->txs);
    }
    if (!phy_config_->quiet || report_tx_status_count_ <= 32) {
      zxlogf(DEBUG, "%s: ReportTxStatus %zu done", name_.c_str(), report_tx_status_count_);
    }
  }

  virtual void ScanComplete(ScanCompleteRequestView request,
                            ScanCompleteCompleter::Sync& completer) override {
    zxlogf(INFO, "%s: ScanComplete(%u)", name_.c_str(), request->status);
    std::lock_guard<std::mutex> guard(wlan_softmac_lock_);
    if (WlantapMac* wlan_softmac = wlan_softmac_devices_.Get(request->wlan_softmac_id)) {
      wlan_softmac->ScanComplete(request->scan_id, request->status);
    }
    zxlogf(DEBUG, "%s: ScanComplete done", name_.c_str());
  }

  // WlantapMac::Listener impl

  virtual void WlantapMacStart(uint16_t wlan_softmac_id) override {
    zxlogf(INFO, "%s: WlantapMacStart id=%u", name_.c_str(), wlan_softmac_id);
    std::lock_guard<std::mutex> guard(fidl_server_lock_);
    if (fidl_server_unbound_) {
      return;
    }
    fidl::Status status =
        fidl::WireSendEvent(user_binding_)->WlanSoftmacStart({.wlan_softmac_id = wlan_softmac_id});
    if (!status.ok()) {
      zxlogf(ERROR, "%s: WlanSoftmacStart() failed", status.status_string());
      return;
    }

    zxlogf(INFO, "%s: WlantapMacStart done", name_.c_str());
  }

  virtual void WlantapMacStop(uint16_t wlan_softmac_id) override {
    zxlogf(INFO, "%s: WlantapMacStop", name_.c_str());
  }

  virtual void WlantapMacQueueTx(uint16_t wlan_softmac_id,
                                 const fuchsia_wlan_softmac::wire::WlanTxPacket& pkt) override {
    size_t pkt_size = pkt.mac_frame.count();
    if (!phy_config_->quiet || report_tx_status_count_ < 32) {
      zxlogf(INFO, "%s: WlantapMacQueueTx id=%u, size=%zu, tx_report_count=%zu", name_.c_str(),
             wlan_softmac_id, pkt_size, report_tx_status_count_);
    }

    std::lock_guard<std::mutex> guard(fidl_server_lock_);
    if (fidl_server_unbound_) {
      zxlogf(INFO, "%s: WlantapMacQueueTx ignored, shutting down", name_.c_str());
      return;
    }

    fidl::Status status = fidl::WireSendEvent(user_binding_)->Tx(ToTxArgs(wlan_softmac_id, pkt));
    if (!status.ok()) {
      zxlogf(ERROR, "%s: Tx() failed", status.status_string());
      return;
    }
    if (!phy_config_->quiet || report_tx_status_count_ < 32) {
      zxlogf(DEBUG, "%s: WlantapMacQueueTx done(%zu bytes), tx_report_count=%zu", name_.c_str(),
             pkt_size, report_tx_status_count_);
    }
  }

  virtual void WlantapMacSetChannel(uint16_t wlan_softmac_id,
                                    const wlan_common::WlanChannel& channel) override {
    if (!phy_config_->quiet) {
      zxlogf(INFO, "%s: WlantapMacSetChannel id=%u, channel=%u", name_.c_str(), wlan_softmac_id,
             channel.primary);
    }
    std::lock_guard<std::mutex> guard(fidl_server_lock_);
    if (fidl_server_unbound_) {
      zxlogf(INFO, "%s: WlantapMacSetChannel ignored, shutting down", name_.c_str());
      return;
    }

    fidl::Status status =
        fidl::WireSendEvent(user_binding_)
            ->SetChannel({.wlan_softmac_id = wlan_softmac_id, .channel = channel});
    if (!status.ok()) {
      zxlogf(ERROR, "%s: SetChannel() failed", status.status_string());
      return;
    }

    if (!phy_config_->quiet) {
      zxlogf(DEBUG, "%s: WlantapMacSetChannel done", name_.c_str());
    }
  }

  virtual void WlantapMacConfigureBss(uint16_t wlan_softmac_id,
                                      const wlan_internal::BssConfig& config) override {
    zxlogf(INFO, "%s: WlantapMacConfigureBss id=%u", name_.c_str(), wlan_softmac_id);
    std::lock_guard<std::mutex> guard(fidl_server_lock_);
    if (fidl_server_unbound_) {
      zxlogf(INFO, "%s: WlantapMacConfigureBss ignored, shutting down", name_.c_str());
      return;
    }

    fidl::Status status =
        fidl::WireSendEvent(user_binding_)
            ->ConfigureBss({.wlan_softmac_id = wlan_softmac_id, .config = config});
    if (!status.ok()) {
      zxlogf(ERROR, "%s: ConfigureBss() failed", status.status_string());
      return;
    }

    zxlogf(DEBUG, "%s: WlantapMacConfigureBss done", name_.c_str());
  }

  virtual void WlantapMacStartScan(uint16_t wlan_softmac_id, const uint64_t scan_id) override {
    zxlogf(INFO, "%s: WlantapMacStartScan id=%u", name_.c_str(), wlan_softmac_id);
    std::lock_guard<std::mutex> guard(fidl_server_lock_);
    if (fidl_server_unbound_) {
      zxlogf(INFO, "%s: WlantapMacStartScan ignored, shutting down", name_.c_str());
      return;
    }

    fidl::Status status = fidl::WireSendEvent(user_binding_)
                              ->StartScan({
                                  .wlan_softmac_id = wlan_softmac_id,
                                  .scan_id = scan_id,
                              });
    if (!status.ok()) {
      zxlogf(ERROR, "%s: StartScan() failed", status.status_string());
      return;
    }
    zxlogf(DEBUG, "%s: WlantapMacStartScan done", name_.c_str());
  }

  virtual void WlantapMacSetKey(uint16_t wlan_softmac_id,
                                const wlan_softmac::WlanKeyConfig& key_config) override {
    zxlogf(INFO, "%s: WlantapMacSetKey id=%u", name_.c_str(), wlan_softmac_id);
    std::lock_guard<std::mutex> guard(fidl_server_lock_);
    if (fidl_server_unbound_) {
      zxlogf(INFO, "%s: WlantapMacSetKey ignored, shutting down", name_.c_str());
      return;
    }

    fidl::Status status =
        fidl::WireSendEvent(user_binding_)->SetKey(ToSetKeyArgs(wlan_softmac_id, key_config));
    if (!status.ok()) {
      zxlogf(ERROR, "%s: SetKey() failed", status.status_string());
      return;
    }

    zxlogf(DEBUG, "%s: WlantapMacSetKey done", name_.c_str());
  }

  zx_device_t* device_;
  const std::shared_ptr<const wlan_tap::WlantapPhyConfig> phy_config_;
  async_dispatcher_t* loop_;
  std::mutex wlan_softmac_lock_;
  DevicePool<WlantapMac, kMaxMacDevices> wlan_softmac_devices_ __TA_GUARDED(wlan_softmac_lock_);
  std::string name_;
  std::mutex fidl_server_lock_;
  fidl::ServerBindingRef<fuchsia_wlan_tap::WlantapPhy> user_binding_
      __TA_GUARDED(fidl_server_lock_);
  bool fidl_server_unbound_ = false;
  const std::chrono::seconds kFidlServerShutdownTimeout = std::chrono::seconds(1);
  bool shutdown_called_ = false;
  size_t report_tx_status_count_ = 0;
};  // namespace

}  // namespace

#define DEV(c) static_cast<WlantapPhy*>(c)
static wlanphy_impl_protocol_ops_t wlanphy_impl_ops = {
    .get_supported_mac_roles =
        [](void* ctx,
           wlan_mac_role_t
               out_supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
           uint8_t* out_supported_mac_roles_count) -> zx_status_t {
      return DEV(ctx)->GetSupportedMacRoles(out_supported_mac_roles_list,
                                            out_supported_mac_roles_count);
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
    .get_country = [](void* ctx, wlanphy_country_t* out_country) -> zx_status_t {
      return DEV(ctx)->GetCountry(out_country);
    },
    .set_ps_mode = [](void* ctx, const wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
      return DEV(ctx)->SetPsMode(ps_mode);
    },
};
#undef DEV

zx_status_t CreatePhy(zx_device_t* wlantapctl, zx::channel user_channel,
                      std::shared_ptr<wlan_tap::WlantapPhyConfig> phy_config,
                      async_dispatcher_t* loop) {
  zxlogf(INFO, "Creating phy");
  auto phy = std::make_unique<WlantapPhy>(wlantapctl, std::move(user_channel), phy_config, loop);
  static zx_protocol_device_t device_ops = {.version = DEVICE_OPS_VERSION,
                                            .unbind = &WlantapPhy::DdkUnbind,
                                            .release = &WlantapPhy::DdkRelease};
  device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                            .name = phy->phy_config_->name.get().data(),
                            .ctx = phy.get(),
                            .ops = &device_ops,
                            .proto_id = ZX_PROTOCOL_WLANPHY_IMPL,
                            .proto_ops = &wlanphy_impl_ops};
  zx_status_t status = device_add(wlantapctl, &args, &phy->device_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
    return status;
  }
  // Transfer ownership to devmgr
  phy.release();
  zxlogf(INFO, "Phy successfully created");
  return ZX_OK;
}

}  // namespace wlan
