// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <cinttypes>
#include <cstring>
#include <memory>
#include <sstream>

#include <wlan/common/bitfield.h>
#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/client/scanner.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/mlme/wlan.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;

#define CHAN_SCHED(c) static_cast<ChannelScheduler*>(c)
#define TIMER_MGR(c) static_cast<TimerManager<TimeoutTarget>*>(c)
#define DEVICE(c) static_cast<DeviceInterface*>(c)

wlan_client_mlme_config_t ClientMlmeDefaultConfig() {
  return wlan_client_mlme_config_t{
      .signal_report_beacon_timeout = 10,
      .ensure_on_channel_time = zx::msec(500).get(),
  };
}

ClientMlme::ClientMlme(DeviceInterface* device) : ClientMlme(device, ClientMlmeDefaultConfig()) {
  debugfn();
}

ClientMlme::ClientMlme(DeviceInterface* device, wlan_client_mlme_config_t config)
    : device_(device),
      on_channel_handler_(this),
      rust_mlme_(nullptr, client_mlme_delete),
      config_(config) {
  debugfn();
}

ClientMlme::~ClientMlme() = default;

zx_status_t ClientMlme::Init() {
  debugfn();

  std::unique_ptr<Timer> timer;
  ObjectId timer_id;
  timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
  timer_id.set_target(to_enum_type(ObjectTarget::kClientMlme));
  zx_status_t status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
  if (status != ZX_OK) {
    errorf("could not create channel scheduler timer: %d\n", status);
    return status;
  }
  timer_mgr_ = std::make_unique<TimerManager<TimeoutTarget>>(std::move(timer));

  chan_sched_ = std::make_unique<ChannelScheduler>(&on_channel_handler_, device_, timer_mgr_.get());
  scanner_ = std::make_unique<Scanner>(device_, chan_sched_.get(), timer_mgr_.get());

  // Initialize Rust dependencies
  auto rust_device = mlme_device_ops_t{
      .device = static_cast<void*>(this->device_),
      .deliver_eth_frame = [](void* device, const uint8_t* data, size_t len) -> zx_status_t {
        return DEVICE(device)->DeliverEthernet({data, len});
      },
      .send_wlan_frame = [](void* device, mlme_out_buf_t buf, uint32_t flags) -> zx_status_t {
        auto pkt = FromRustOutBuf(buf);
        return DEVICE(device)->SendWlan(std::move(pkt), flags);
      },
      .get_sme_channel = [](void* device) -> zx_handle_t {
        return DEVICE(device)->GetSmeChannelRef();
      },
      .set_wlan_channel = [](void* device, wlan_channel_t chan) -> zx_status_t {
        return DEVICE(device)->SetChannel(chan);
      },
      .get_wlan_channel = [](void* device) -> wlan_channel_t {
        return DEVICE(device)->GetState()->channel();
      },
      .set_key = [](void* device, wlan_key_config_t* key) -> zx_status_t {
        return DEVICE(device)->SetKey(key);
      },
      .configure_bss = [](void* device, wlan_bss_config_t* cfg) -> zx_status_t {
        return DEVICE(device)->ConfigureBss(cfg);
      },
      .enable_beaconing = [](void* device, const uint8_t* beacon_tmpl_data, size_t beacon_tmpl_len,
                             size_t tim_ele_offset, uint16_t beacon_interval) -> zx_status_t {
        // The client never needs to enable beaconing.
        return ZX_ERR_NOT_SUPPORTED;
      },
      .disable_beaconing = [](void* device) -> zx_status_t {
        // The client never needs to disable beaconing.
        return ZX_ERR_NOT_SUPPORTED;
      },
      .configure_assoc = [](void* device, wlan_assoc_ctx_t* assoc_ctx) -> zx_status_t {
        return DEVICE(device)->ConfigureAssoc(assoc_ctx);
      },
      .clear_assoc = [](void* device, const uint8_t(*addr)[6]) -> zx_status_t {
        return DEVICE(device)->ClearAssoc(common::MacAddr(*addr));
      },
  };
  auto scheduler = wlan_scheduler_ops_t{
      .cookie = static_cast<void*>(this->timer_mgr_.get()),
      .now = [](void* cookie) -> zx_time_t { return TIMER_MGR(cookie)->Now().get(); },
      .schedule = [](void* cookie, int64_t deadline) -> wlan_scheduler_event_id_t {
        TimeoutId id = {};
        TIMER_MGR(cookie)->Schedule(zx::time(deadline), TimeoutTarget::kRust, &id);
        return {._0 = id.raw()};
      },
      .cancel = [](void* cookie,
                   wlan_scheduler_event_id_t id) { TIMER_MGR(cookie)->Cancel(TimeoutId(id._0)); },
  };
  auto chan_sched_proxy = wlan_cpp_chan_sched_t{
      .chan_sched = static_cast<void*>(this->chan_sched_.get()),
      .ensure_on_channel =
          [](void* chan_sched, zx_time_t end) {
            CHAN_SCHED(chan_sched)->EnsureOnChannel(zx::time(end));
          },
  };
  rust_mlme_ = RustClientMlme(
      client_mlme_new(config_, rust_device, rust_buffer_provider, scheduler, chan_sched_proxy),
      client_mlme_delete);

  return status;
}

zx_status_t ClientMlme::HandleTimeout(const ObjectId id) {
  if (id.target() != to_enum_type(ObjectTarget::kClientMlme)) {
    ZX_DEBUG_ASSERT(0);
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = timer_mgr_->HandleTimeout([&](auto now, auto target, auto timeout_id) {
    switch (target) {
      case TimeoutTarget::kDefault:
        if (sta_ != nullptr) {
          sta_->HandleTimeout(now, target, timeout_id);
        } else {
          warnf("timeout for unknown STA: %zu\n", id.mac());
        }
        break;
      case TimeoutTarget::kRust:
        wlan_client_sta_t* rust_client;
        if (sta_ != nullptr) {
          rust_client = sta_->GetRustClientSta();
        } else {
          rust_client = nullptr;
        }
        client_mlme_timeout_fired(rust_mlme_.get(), rust_client,
                                  wlan_scheduler_event_id_t{._0 = timeout_id.raw()});
        break;
      case TimeoutTarget::kChannelScheduler:
        chan_sched_->HandleTimeout();
        break;
      case TimeoutTarget::kScanner:
        scanner_->HandleTimeout();
        break;
    }
  });

  if (status != ZX_OK) {
    errorf("failed to rearm the timer after handling the timeout: %s",
           zx_status_get_string(status));
  }
  return status;
}

void ClientMlme::HwScanComplete(uint8_t result_code) {
  if (result_code == WLAN_HW_SCAN_SUCCESS) {
    scanner_->HandleHwScanComplete();
  } else {
    scanner_->HandleHwScanAborted();
  }
}

zx_status_t ClientMlme::HandleEncodedMlmeMsg(fbl::Span<const uint8_t> msg) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ClientMlme::HandleMlmeMsg(const BaseMlmeMsg& msg) {
  if (auto scan_req = msg.As<wlan_mlme::ScanRequest>()) {
    // Let the Scanner handle all MLME-SCAN.requests.
    return scanner_->HandleMlmeScanReq(*scan_req);
  } else if (auto join_req = msg.As<wlan_mlme::JoinRequest>()) {
    // An MLME-JOIN-request will synchronize the MLME with the request's BSS.
    // Synchronization is mandatory for spawning a client and starting its
    // association flow.

    Unjoin();
    return HandleMlmeJoinReq(*join_req);
  } else if (!join_ctx_.has_value()) {
    warnf("rx'ed MLME message (ordinal: %lu) before synchronizing with a BSS\n", msg.ordinal());
    return ZX_ERR_BAD_STATE;
  }

  // TODO(hahnr): Keys should not be handled in the STA and instead in the MLME.
  // For now, shortcut into the STA and leave this change as a follow-up.
  if (auto setkeys_req = msg.As<wlan_mlme::SetKeysRequest>()) {
    if (sta_ != nullptr) {
      return sta_->SetKeys(setkeys_req->body()->keylist);
    } else {
      warnf("rx'ed MLME message (ordinal: %lu) before authenticating with a BSS\n", msg.ordinal());
      return ZX_ERR_BAD_STATE;
    }
  }

  // All remaining message must use the same BSS this MLME synchronized to
  // before.
  auto peer_addr = service::GetPeerAddr(msg);
  if (!peer_addr.has_value()) {
    warnf("rx'ed unsupported MLME msg (ordinal: %lu)\n", msg.ordinal());
    return ZX_ERR_INVALID_ARGS;
  } else if (peer_addr.value() != join_ctx_->bssid()) {
    warnf(
        "rx'ed MLME msg (ordinal: %lu) with unexpected peer addr; expected: %s "
        "; actual: %s\n",
        msg.ordinal(), join_ctx_->bssid().ToString().c_str(), peer_addr->ToString().c_str());
    return ZX_ERR_INVALID_ARGS;
  }

  // This will spawn a new client instance and start association flow.
  if (auto auth_req = msg.As<wlan_mlme::AuthenticateRequest>()) {
    auto status = SpawnStation();
    if (status != ZX_OK) {
      errorf("error spawning STA: %d\n", status);
      return service::SendAuthConfirm(device_, join_ctx_->bssid(),
                                      wlan_mlme::AuthenticateResultCodes::REFUSED);
    }

    // Let station handle the request itself.
    return sta_->Authenticate(auth_req->body()->auth_type, auth_req->body()->auth_failure_timeout);
  }

  // If the STA exists, forward all incoming MLME messages.
  if (sta_ == nullptr) {
    warnf("rx'ed MLME message (ordinal: %lu) before authenticating with a BSS\n", msg.ordinal());
    return ZX_ERR_BAD_STATE;
  }

  if (auto deauth_req = msg.As<wlan_mlme::DeauthenticateRequest>()) {
    return sta_->Deauthenticate(deauth_req->body()->reason_code);
  } else if (auto assoc_req = msg.As<wlan_mlme::AssociateRequest>()) {
    return sta_->Associate(*assoc_req->body());
  } else if (auto eapol_req = msg.As<wlan_mlme::EapolRequest>()) {
    auto body = eapol_req->body();
    return sta_->SendEapolFrame(body->data, common::MacAddr(body->src_addr),
                                common::MacAddr(body->dst_addr));
  } else if (auto setctrlport_req = msg.As<wlan_mlme::SetControlledPortRequest>()) {
    sta_->UpdateControlledPort(setctrlport_req->body()->state);
    return ZX_OK;
  } else {
    warnf("rx'ed unsupported MLME message for client; ordinal: %lu\n", msg.ordinal());
    return ZX_ERR_BAD_STATE;
  }
}

zx_status_t ClientMlme::HandleFramePacket(std::unique_ptr<Packet> pkt) {
  switch (pkt->peer()) {
    case Packet::Peer::kEthernet: {
      // For outbound frame (Ethernet frame), hand to station directly so
      // station sends frame to device when on channel, or buffers it when
      // off channel.
      if (sta_ != nullptr) {
        if (auto eth_frame = EthFrameView::CheckType(pkt.get()).CheckLength()) {
          return sta_->HandleEthFrame(eth_frame.IntoOwned(std::move(pkt)));
        }
      }
      return ZX_ERR_BAD_STATE;
    }
    case Packet::Peer::kWlan: {
      chan_sched_->HandleIncomingFrame(std::move(pkt));
      return ZX_OK;
    }
    default:
      errorf("unknown Packet peer: %u\n", pkt->peer());
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t ClientMlme::HandleMlmeJoinReq(const MlmeMsg<wlan_mlme::JoinRequest>& req) {
  debugfn();

  wlan_mlme::BSSDescription bss;
  auto status = req.body()->selected_bss.Clone(&bss);
  if (status != ZX_OK) {
    errorf("error cloning MLME-JOIN.request: %d\n", status);
    return status;
  }
  JoinContext join_ctx(std::move(bss), req.body()->phy, req.body()->cbw);

  auto join_chan = join_ctx.channel();
  debugjoin("setting channel to %s\n", common::ChanStrLong(join_chan).c_str());
  status = chan_sched_->SetChannel(join_chan);
  if (status != ZX_OK) {
    errorf("could not set WLAN channel to %s: %d\n", common::ChanStrLong(join_chan).c_str(),
           status);
    service::SendJoinConfirm(device_, wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
    return status;
  }

  // Notify driver about BSS.
  wlan_bss_config_t cfg{
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .remote = true,
  };
  join_ctx.bssid().CopyTo(cfg.bssid);
  status = device_->ConfigureBss(&cfg);
  if (status != ZX_OK) {
    errorf("error configuring BSS in driver; aborting: %d\n", status);
    // TODO(hahnr): JoinResultCodes needs to define better result codes.
    return service::SendJoinConfirm(device_, wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
  }

  join_ctx_ = std::move(join_ctx);

  // Send confirmation for successful synchronization to SME.
  return service::SendJoinConfirm(device_, wlan_mlme::JoinResultCodes::SUCCESS);
}

zx_status_t ClientMlme::SpawnStation() {
  if (!join_ctx_.has_value()) {
    return ZX_ERR_BAD_STATE;
  }

  auto client = std::make_unique<Station>(device_, &config_, timer_mgr_.get(), chan_sched_.get(),
                                          &join_ctx_.value(), rust_mlme_.get());

  if (!client) {
    return ZX_ERR_INTERNAL;
  }
  sta_ = std::move(client);
  return ZX_OK;
}

void ClientMlme::OnChannelHandlerImpl::PreSwitchOffChannel() {
  debugfn();
  if (mlme_->sta_ != nullptr) {
    mlme_->sta_->PreSwitchOffChannel();
  }
}

void ValidateOnChannelFrame(const Packet& pkt) {
  debugfn();
  // Only WLAN frames are handed to channel handler since all Ethernet frames
  // are handed over to the station directly.
  ZX_DEBUG_ASSERT(pkt.peer() == Packet::Peer::kWlan);
}

void ClientMlme::OnChannelHandlerImpl::HandleOnChannelFrame(std::unique_ptr<Packet> packet) {
  debugfn();
  ValidateOnChannelFrame(*packet);

  if (auto mgmt_frame = MgmtFrameView<>::CheckType(packet.get()).CheckLength()) {
    if (auto bcn_frame = mgmt_frame.CheckBodyType<Beacon>().CheckLength()) {
      mlme_->scanner_->HandleBeacon(bcn_frame);
    } else if (auto probe_frame = mgmt_frame.CheckBodyType<ProbeResponse>().CheckLength()) {
      mlme_->scanner_->HandleProbeResponse(probe_frame);
    }
  }

  if (mlme_->sta_ != nullptr) {
    mlme_->sta_->HandleWlanFrame(std::move(packet));
  }
}

void ClientMlme::OnChannelHandlerImpl::ReturnedOnChannel() {
  debugfn();
  if (mlme_->sta_ != nullptr) {
    mlme_->sta_->BackToMainChannel();
  }
}

wlan_stats::MlmeStats ClientMlme::GetMlmeStats() const {
  wlan_stats::MlmeStats mlme_stats{};
  if (sta_ != nullptr) {
    mlme_stats.set_client_mlme_stats(sta_->stats());
  }
  return mlme_stats;
}

void ClientMlme::ResetMlmeStats() {
  if (sta_ != nullptr) {
    sta_->ResetStats();
  }
}

bool ClientMlme::OnChannel() {
  if (chan_sched_ != nullptr) {
    return chan_sched_->OnChannel();
  }
  return false;
}

void ClientMlme::Unjoin() {
  join_ctx_.reset();
  sta_.reset();
}

}  // namespace wlan
