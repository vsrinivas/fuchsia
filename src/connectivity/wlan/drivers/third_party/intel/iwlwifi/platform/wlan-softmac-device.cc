// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlan-softmac-device.h"

#include <zircon/assert.h>
#include <zircon/status.h>

#include <memory>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-sta.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/scoped_utils.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/stats.h"

#define CHECK_DELETE_IN_PROGRESS_WITHOUT_ERRSYNTAX(mvmvif, resp)        \
  do {                                                                  \
    if (mvmvif->delete_in_progress) {                                   \
      IWL_WARN(mvmvif, "Interface is in the process of being deleted"); \
      completer.buffer(arena).Reply(resp);                              \
      return;                                                           \
    }                                                                   \
  } while (0)

#define CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif)                 \
  do {                                                                  \
    if (mvmvif->delete_in_progress) {                                   \
      IWL_WARN(mvmvif, "Interface is in the process of being deleted"); \
      completer.buffer(arena).ReplyError(ZX_ERR_BAD_STATE);             \
      return;                                                           \
    }                                                                   \
  } while (0)

namespace wlan::iwlwifi {

WlanSoftmacDevice::WlanSoftmacDevice(zx_device* parent, iwl_trans* drvdata, uint16_t iface_id,
                                     struct iwl_mvm_vif* mvmvif)
    : ddk::Device<WlanSoftmacDevice, ddk::Initializable, ddk::Unbindable, ddk::ServiceConnectable>(
          parent),
      mvmvif_(mvmvif),
      drvdata_(drvdata),
      iface_id_(iface_id),
      mac_started(false) {}

WlanSoftmacDevice::~WlanSoftmacDevice() {}

// Max size of WlanSoftmacInfo.
constexpr size_t kWlanSoftmacInfoBufferSize =
    fidl::MaxSizeInChannel<fuchsia_wlan_softmac::wire::WlanSoftmacInfo,
                           fidl::MessageDirection::kSending>();

void WlanSoftmacDevice::Query(fdf::Arena& arena, QueryCompleter::Sync& completer) {
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);

  fidl::Arena<kWlanSoftmacInfoBufferSize> table_arena;
  fuchsia_wlan_softmac::wire::WlanSoftmacInfo softmac_info;
  zx_status_t status = mac_query(mvmvif_, &softmac_info, table_arena);

  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed query: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess(softmac_info);
}

void WlanSoftmacDevice::QueryDiscoverySupport(fdf::Arena& arena,
                                              QueryDiscoverySupportCompleter::Sync& completer) {
  fuchsia_wlan_common::wire::DiscoverySupport out_resp;
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  mac_query_discovery_support(&out_resp);
  completer.buffer(arena).ReplySuccess(out_resp);
}

void WlanSoftmacDevice::QueryMacSublayerSupport(fdf::Arena& arena,
                                                QueryMacSublayerSupportCompleter::Sync& completer) {
  fuchsia_wlan_common::wire::MacSublayerSupport out_resp;
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  mac_query_mac_sublayer_support(&out_resp);
  completer.buffer(arena).ReplySuccess(out_resp);
}

void WlanSoftmacDevice::QuerySecuritySupport(fdf::Arena& arena,
                                             QuerySecuritySupportCompleter::Sync& completer) {
  fuchsia_wlan_common::wire::SecuritySupport out_resp;
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  mac_query_security_support(&out_resp);
  completer.buffer(arena).ReplySuccess(out_resp);
}

void WlanSoftmacDevice::QuerySpectrumManagementSupport(
    fdf::Arena& arena, QuerySpectrumManagementSupportCompleter::Sync& completer) {
  fuchsia_wlan_common::wire::SpectrumManagementSupport out_resp;
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  mac_query_spectrum_management_support(&out_resp);
  completer.buffer(arena).ReplySuccess(out_resp);
}

void WlanSoftmacDevice::Start(StartRequestView request, fdf::Arena& arena,
                              StartCompleter::Sync& completer) {
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  zx::channel out_mlme_channel;

  client_ = fdf::WireSharedClient<fuchsia_wlan_softmac::WlanSoftmacIfc>(std::move(request->ifc),
                                                                        client_dispatcher_.get());

  zx_status_t status = mac_start(mvmvif_, this, (zx_handle_t*)(&out_mlme_channel));
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed mac start: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);

    return;
  }

  mac_started = true;
  completer.buffer(arena).ReplySuccess(std::move(out_mlme_channel));
}

void WlanSoftmacDevice::Stop(fdf::Arena& arena, StopCompleter::Sync& completer) {
  // Remove the stop logic from destructor if higher layer calls this function correctly.
  if (mac_started) {
    ap_mvm_sta_.reset();
    mac_stop(mvmvif_);
  }
  completer.buffer(arena).Reply();
}

void WlanSoftmacDevice::QueueTx(QueueTxRequestView request, fdf::Arena& arena,
                                QueueTxCompleter::Sync& completer) {
  iwl_stats_inc(IWL_STATS_CNT_DATA_FROM_MLME);
  // Delayed transmission is never used right now.
  if (ap_mvm_sta_ == nullptr) {
    IWL_ERR(this, "%s() No ap_sta is found.\n", __func__);
    completer.buffer(arena).ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  zx_status_t status = ZX_OK;
  const auto& packet = request->packet;
  if (packet.mac_frame.count() > WLAN_MSDU_MAX_LEN) {
    IWL_ERR(mvmvif_, "Frame size is to large (%lu). expect less than %lu.\n",
            packet.mac_frame.count(), WLAN_MSDU_MAX_LEN);
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  ieee80211_mac_packet mac_packet = {};
  mac_packet.common_header =
      reinterpret_cast<const ieee80211_frame_header*>(packet.mac_frame.data());
  mac_packet.header_size = ieee80211_get_header_len(mac_packet.common_header);
  if (mac_packet.header_size > packet.mac_frame.count()) {
    IWL_ERR(mvmvif_, "TX packet header size %zu too large for data size %zu\n",
            mac_packet.header_size, packet.mac_frame.count());
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  mac_packet.body = packet.mac_frame.data() + mac_packet.header_size;
  mac_packet.body_size = packet.mac_frame.count() - mac_packet.header_size;
  if (ieee80211_pkt_is_protected(mac_packet.common_header)) {
    switch (ieee80211_get_frame_type(mac_packet.common_header)) {
      case ieee80211_frame_type::IEEE80211_FRAME_TYPE_MGMT:
        mac_packet.info.control.hw_key = ap_mvm_sta_->GetKey(WLAN_KEY_TYPE_IGTK);
        break;
      case ieee80211_frame_type::IEEE80211_FRAME_TYPE_DATA:
        mac_packet.info.control.hw_key = ap_mvm_sta_->GetKey(WLAN_KEY_TYPE_PAIRWISE);
        break;
      default:
        break;
    }
  }

  auto lock = std::lock_guard(mvmvif_->mvm->mutex);
  status = iwl_mvm_mac_tx(mvmvif_, ap_mvm_sta_->iwl_mvm_sta(), &mac_packet);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed mac tx: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  // Delayed transmission is never used right now, setting enqueue_pending to false;
  completer.buffer(arena).ReplySuccess(false);
}

// Reject the request that firmware doesn't allow. See fxb/89911 for more context.
bool WlanSoftmacDevice::IsValidChannel(const fuchsia_wlan_common::wire::WlanChannel* channel) {
  if (channel->cbw == fuchsia_wlan_common::ChannelBandwidth::kCbw40 ||
      channel->cbw == fuchsia_wlan_common::ChannelBandwidth::kCbw40Below) {
    if (channel->primary >= 10 && channel->primary <= 14) {
      IWL_WARN(mvmvif_, "The 40%sMHz bandwidth is not supported on the channel %d.\n",
               channel->cbw == fuchsia_wlan_common::ChannelBandwidth::kCbw40Below ? "-" : "",
               channel->primary);
      return false;
    }
  }

  if (channel->primary <= 14 && channel->cbw >= fuchsia_wlan_common::ChannelBandwidth::kCbw80) {
    IWL_WARN(mvmvif_, "The 80+MHz bandwidth is not supported on the 2.4GHz band (channel %d).\n",
             channel->primary);
    return false;
  }

  return true;
}

void WlanSoftmacDevice::SetChannel(SetChannelRequestView request, fdf::Arena& arena,
                                   SetChannelCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;

  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);

  if (!IsValidChannel(&request->chan)) {
    IWL_WARN(this, "%s() Invalid channel.\n", __func__);
    completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // If the AP sta already exists, it probably was left from the previous association attempt.
  // Remove it first.
  wlan_channel_t channel = {
      .primary = request->chan.primary,
      .cbw = (channel_bandwidth_t)request->chan.cbw,
      .secondary80 = request->chan.secondary80,
  };

  status = mac_set_channel(mvmvif_, &channel);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed mac set channel: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}  // namespace wlan::iwlwifi

void WlanSoftmacDevice::ConfigureBss(ConfigureBssRequestView request, fdf::Arena& arena,
                                     ConfigureBssCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;
  if (ap_mvm_sta_ != nullptr) {
    IWL_INFO(this, "AP sta already exist.  Unassociate it first.\n");
    if ((status = mac_unconfigure_bss(mvmvif_)) != ZX_OK) {
      IWL_ERR(this, "failed mac unconfigure bss: %s\n",
              zx_status_get_string(status));
      completer.buffer(arena).ReplyError(status);
      return;
    }
    ap_mvm_sta_.reset();
  }
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  if ((status = mac_configure_bss(mvmvif_, &request->config)) != ZX_OK) {
    IWL_ERR(this, "failed mac configure bss: %s\n", zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  ZX_DEBUG_ASSERT(mvmvif_->mac_role == WLAN_MAC_ROLE_CLIENT);
  std::unique_ptr<MvmSta> ap_mvm_sta;
  if ((status = MvmSta::Create(mvmvif_, request->config.bssid.begin(), &ap_mvm_sta)) != ZX_OK) {
    IWL_ERR(this, "failed creating MvmSta: %s\n", zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  ap_mvm_sta_ = std::move(ap_mvm_sta);
  completer.buffer(arena).ReplyError(ZX_OK);
}

void WlanSoftmacDevice::EnableBeaconing(EnableBeaconingRequestView request, fdf::Arena& arena,
                                        EnableBeaconingCompleter::Sync& completer) {
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  zx_status_t status = mac_enable_beaconing(mvmvif_, &request->bcn_cfg);
  if (status != ZX_OK) {
    // Expected for now since this is not supported yet in iwlwifi driver.
    IWL_ERR(this, "%s() failed mac enable beaconing: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void WlanSoftmacDevice::ConfigureBeacon(ConfigureBeaconRequestView request, fdf::Arena& arena,
                                        ConfigureBeaconCompleter::Sync& completer) {
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  zx_status_t status = mac_configure_beacon(mvmvif_, &request->packet);
  if (status != ZX_OK) {
    // Expected for now since this is not supported yet in iwlwifi driver.
    IWL_ERR(this, "%s() failed mac configure beacon: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void WlanSoftmacDevice::SetKey(SetKeyRequestView request, fdf::Arena& arena,
                               SetKeyCompleter::Sync& completer) {
  if (ap_mvm_sta_ == nullptr) {
    IWL_ERR(this, "%s() Ap sta does not exist.\n", __func__);
    completer.buffer(arena).ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  zx_status_t status = ap_mvm_sta_->SetKey(&request->key_config);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed SetKey: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void WlanSoftmacDevice::ConfigureAssoc(ConfigureAssocRequestView request, fdf::Arena& arena,
                                       ConfigureAssocCompleter::Sync& completer) {
  if (ap_mvm_sta_ == nullptr) {
    IWL_ERR(this, "%s() Ap sta does not exist.\n", __func__);
    completer.buffer(arena).ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  zx_status_t status = mac_configure_assoc(mvmvif_, &request->assoc_ctx);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed mac configure assoc: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void WlanSoftmacDevice::ClearAssoc(ClearAssocRequestView request, fdf::Arena& arena,
                                   ClearAssocCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;

  if (ap_mvm_sta_ == nullptr) {
    IWL_ERR(this, "%s() Ap sta does not exist.\n", __func__);
    completer.buffer(arena).ReplyError(ZX_ERR_BAD_STATE);
    return;
  }
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);

  // Mark the station is no longer associated. This must be set before we start operating on the STA
  // instance.
  mvmvif_->bss_conf.assoc = false;
  ap_mvm_sta_.reset();

  if ((status = mac_clear_assoc(mvmvif_, request->peer_addr.data())) != ZX_OK) {
    IWL_ERR(this, "%s() failed clear assoc: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess();
}

void WlanSoftmacDevice::StartPassiveScan(StartPassiveScanRequestView request, fdf::Arena& arena,
                                         StartPassiveScanCompleter::Sync& completer) {
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  uint64_t out_scan_id;
  zx_status_t status = mac_start_passive_scan(mvmvif_, &request->args, &out_scan_id);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed start passive scan: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess(out_scan_id);
}

void WlanSoftmacDevice::StartActiveScan(StartActiveScanRequestView request, fdf::Arena& arena,
                                        StartActiveScanCompleter::Sync& completer) {
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  uint64_t out_scan_id;
  zx_status_t status = mac_start_active_scan(mvmvif_, &request->args, &out_scan_id);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed start active scan: %s\n", __func__, zx_status_get_string(status));
    completer.buffer(arena).ReplyError(status);
    return;
  }

  completer.buffer(arena).ReplySuccess(out_scan_id);
}

void WlanSoftmacDevice::CancelScan(CancelScanRequestView request, fdf::Arena& arena,
                                   CancelScanCompleter::Sync& completer) {
  // TODO(fxbug.dev/107743): Implement.
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void WlanSoftmacDevice::UpdateWmmParams(UpdateWmmParamsRequestView request, fdf::Arena& arena,
                                        UpdateWmmParamsCompleter::Sync& completer) {
  IWL_ERR(this, "%s() needs porting\n", __func__);
  CHECK_DELETE_IN_PROGRESS_WITH_ERRSYNTAX(mvmvif_);
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t WlanSoftmacDevice::InitClientDispatcher() {
  // Create dispatcher for FIDL client of WlanSoftmacIfc protocol.
  auto dispatcher = fdf::Dispatcher::Create(0, "wlansoftmacifc_client", [&](fdf_dispatcher_t*) {
    if (unbind_txn_)
      unbind_txn_->Reply();
  });

  if (dispatcher.is_error()) {
    IWL_ERR(this, "%s(): Dispatcher created failed%s\n", __func__,
            zx_status_get_string(dispatcher.status_value()));
    return dispatcher.status_value();
  }

  client_dispatcher_ = *std::move(dispatcher);

  return ZX_OK;
}

zx_status_t WlanSoftmacDevice::InitServerDispatcher() {
  // Create dispatcher for FIDL server of WlanSoftmac protocol.
  auto dispatcher = fdf::Dispatcher::Create(
      0, "wlansoftmac_server", [&](fdf_dispatcher_t*) { client_dispatcher_.ShutdownAsync(); });
  if (dispatcher.is_error()) {
    IWL_ERR(this, "%s(): Dispatcher created failed%s\n", __func__,
            zx_status_get_string(dispatcher.status_value()));
    return dispatcher.status_value();
  }
  server_dispatcher_ = *std::move(dispatcher);

  return ZX_OK;
}

void WlanSoftmacDevice::DdkInit(ddk::InitTxn txn) {
  zx_status_t ret = InitServerDispatcher();
  ZX_ASSERT_MSG(ret == ZX_OK, "Creating dispatcher error: %s\n", zx_status_get_string(ret));

  ret = InitClientDispatcher();
  ZX_ASSERT_MSG(ret == ZX_OK, "Creating dispatcher error: %s\n", zx_status_get_string(ret));

  txn.Reply(mac_init(mvmvif_, drvdata_, zxdev(), iface_id_));
}

void WlanSoftmacDevice::DdkRelease() {
  IWL_DEBUG_INFO(this, "Releasing iwlwifi mac-device\n");
  if (mac_started) {
    ap_mvm_sta_.reset();
    mac_stop(mvmvif_);
  }
  mac_release(mvmvif_);
  delete this;
}

void WlanSoftmacDevice::DdkUnbind(ddk::UnbindTxn txn) {
  IWL_DEBUG_INFO(this, "Unbinding iwlwifi mac-device\n");
  // Saving the input UnbindTxn to the device, ::ddk::UnbindTxn::Reply() will be called with this
  // UnbindTxn in the shutdown callback of client_dispatcher_, and the ShutdownAsync() for
  // client_dispatcher_ is called in the shutdown callback of server_dispatcher_,  so that we can
  // make sure DdkUnbind() won't end before the dispatchers' shutdown.
  unbind_txn_ = std::move(txn);
  mac_unbind(mvmvif_);
  server_dispatcher_.ShutdownAsync();
}

zx_status_t WlanSoftmacDevice::DdkServiceConnect(const char* service_name, fdf::Channel channel) {
  // Ensure they are requesting the correct protocol.
  if (std::string_view(service_name) !=
      fidl::DiscoverableProtocolName<fuchsia_wlan_softmac::WlanSoftmac>) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  fdf::ServerEnd<fuchsia_wlan_softmac::WlanSoftmac> server_end(std::move(channel));
  fdf::BindServer<fdf::WireServer<fuchsia_wlan_softmac::WlanSoftmac>>(server_dispatcher_.get(),
                                                                      std::move(server_end), this);
  return ZX_OK;
}

void WlanSoftmacDevice::Recv(fuchsia_wlan_softmac::wire::WlanRxPacket* rx_packet) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    IWL_ERR(this, "Failed to create Arena in WlanSoftmacDevice::Recv().\n");
    return;
  }
  auto result = client_.sync().buffer(*std::move(arena))->Recv(*rx_packet);
  if (!result.ok()) {
    IWL_ERR(this, "Failed to send rx frames up in WlanSoftmacDevice::Recv(). Status: %d\n",
            result.status());
  }
}

void WlanSoftmacDevice::ScanComplete(const zx_status_t status, const uint64_t scan_id) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    IWL_ERR(this,
        "Failed to create Arena in WlanSoftmacDevice::ScanComplete(). "
        "scan_id=%zu, status=%s\n", scan_id, zx_status_get_string(status));
    return;
  }

  auto result = client_.sync().buffer(*std::move(arena))->ScanComplete(status, scan_id);
  if (!result.ok()) {
    IWL_ERR(this,
        "Failed to send scan complete notification up in WlanSoftmacDevice::ScanComplete(). "
        "result.status: %d, scan_id=%zu, status=%s\n",
        result.status(), scan_id, zx_status_get_string(status));
  }
}

}  // namespace wlan::iwlwifi
