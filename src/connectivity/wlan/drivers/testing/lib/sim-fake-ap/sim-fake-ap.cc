// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"

#include <fuchsia/wlan/common/c/banjo.h>
#include <zircon/assert.h>

namespace wlan::simulation {

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

void FakeAp::SetChannel(const wlan_channel_t& channel) {
  // Time until next beacon.
  zx::duration diff_to_next_beacon = beacon_state_.next_beacon_time - environment_->GetTime();

  // If any station is associating with this AP, trigger channel switch.
  if (GetNumAssociatedClient() > 0 && beacon_state_.is_beaconing &&
      (csa_beacon_interval_ >= diff_to_next_beacon)) {
    // If a new CSA is triggered, then it will override the previous one, and schedule a new channel
    // switch time.
    uint8_t cs_count = 0;
    // This is the time period start from next beacon to the end of CSA beacon interval.
    zx::duration cover = csa_beacon_interval_ - diff_to_next_beacon;
    // This value is zero means next beacon is scheduled at the same time as CSA beacon interval
    // end, and due to the mechanism of sim_env, this beacon will be sent out because it's scheduled
    // earlier than we actually change channel.
    if (cover.get() == 0) {
      cs_count = 1;
    } else {
      cs_count = cover / beacon_state_.beacon_frame_.interval_ +
                 (cover % beacon_state_.beacon_frame_.interval_ ? 1 : 0);
    }

    if (beacon_state_.is_switching_channel) {
      CancelNotification(beacon_state_.channel_switch_notification_id);
    }

    beacon_state_.beacon_frame_.AddCsaIe(channel, cs_count);
    beacon_state_.channel_after_csa = channel;

    environment_->ScheduleNotification(std::bind(&FakeAp::HandleStopCsaBeaconNotification, this),
                                       csa_beacon_interval_,
                                       &beacon_state_.channel_switch_notification_id);
    beacon_state_.is_switching_channel = true;
  } else {
    tx_info_.channel = channel;
  }
}

void FakeAp::SetBssid(const common::MacAddr& bssid) {
  bssid_ = bssid;
  beacon_state_.beacon_frame_.bssid_ = bssid;
}

void FakeAp::SetSsid(const cssid_t& ssid) {
  ssid_ = ssid;
  beacon_state_.beacon_frame_.AddSsidIe(ssid);
}

void FakeAp::SetCsaBeaconInterval(zx::duration interval) {
  // Meaningless to set CSA_beacon_interval to 0.
  ZX_ASSERT(interval.get() != 0);
  csa_beacon_interval_ = interval;
}

zx_status_t FakeAp::SetSecurity(struct Security sec) {
  // Should we clean the associated client list when we change security protocol?
  if (!clients_.empty())
    return ZX_ERR_BAD_STATE;

  security_ = sec;
  if (sec.cipher_suite != IEEE80211_CIPHER_SUITE_NONE)
    beacon_state_.beacon_frame_.capability_info_.set_privacy(1);
  if (sec.cipher_suite == IEEE80211_CIPHER_SUITE_WEP_40 ||
      sec.cipher_suite == IEEE80211_CIPHER_SUITE_TKIP ||
      sec.cipher_suite == IEEE80211_CIPHER_SUITE_WEP_104) {
  }

  return ZX_OK;
}

bool FakeAp::CanReceiveChannel(const wlan_channel_t& channel) {
  // For now, require an exact match
  return ((channel.primary == tx_info_.channel.primary) && (channel.cbw == tx_info_.channel.cbw) &&
          (channel.secondary80 == tx_info_.channel.secondary80));
}

void FakeAp::ScheduleNextBeacon() {
  environment_->ScheduleNotification(std::bind(&FakeAp::HandleBeaconNotification, this),
                                     beacon_state_.beacon_frame_.interval_,
                                     &beacon_state_.beacon_notification_id);
  beacon_state_.next_beacon_time = environment_->GetTime() + beacon_state_.beacon_frame_.interval_;
}

void FakeAp::EnableBeacon(zx::duration beacon_period) {
  if (beacon_state_.is_beaconing) {
    // If we're already beaconing, we want to cancel any pending scheduled beacons before
    // restarting with the new beacon period.
    DisableBeacon();
  }

  // First beacon is sent out immediately
  SimBeaconFrame tmp_beacon_frame(beacon_state_.beacon_frame_);
  if (CheckIfErrInjBeaconEnabled()) {
    tmp_beacon_frame = beacon_state_.beacon_mutator(tmp_beacon_frame);
  }
  environment_->Tx(tmp_beacon_frame, tx_info_, this);

  beacon_state_.is_beaconing = true;
  beacon_state_.beacon_frame_.interval_ = beacon_period;

  ScheduleNextBeacon();
}

void FakeAp::DisableBeacon() {
  // If it is not beaconing, do nothing.
  if (!beacon_state_.is_beaconing) {
    return;
  }

  // If we stop beaconing when channel is switching, we cancel the channel switch event and directly
  // set channel to new channel.
  if (beacon_state_.is_switching_channel) {
    tx_info_.channel = beacon_state_.channel_after_csa;
    beacon_state_.is_switching_channel = false;
    CancelNotification(beacon_state_.channel_switch_notification_id);
  }

  beacon_state_.is_beaconing = false;
  CancelNotification(beacon_state_.beacon_notification_id);
}

inline void FakeAp::CancelNotification(uint64_t id) {
  ZX_ASSERT(environment_->CancelNotification(id) == ZX_OK);
}

std::shared_ptr<FakeAp::Client> FakeAp::AddClient(common::MacAddr mac_addr) {
  auto client = std::make_shared<Client>(mac_addr, Client::NOT_AUTHENTICATED);
  clients_.push_back(client);
  return client;
}

std::shared_ptr<FakeAp::Client> FakeAp::FindClient(wlan::common::MacAddr mac_addr) {
  for (auto it = clients_.begin(); it != clients_.end(); it++) {
    if (mac_addr == (*it)->mac_addr_) {
      return *it;
    }
  }

  return std::shared_ptr<FakeAp::Client>(nullptr);
}

void FakeAp::RemoveClient(common::MacAddr mac_addr) {
  for (auto it = clients_.begin(); it != clients_.end();) {
    if (mac_addr == (*it)->mac_addr_) {
      it = clients_.erase(it);
    } else {
      it++;
    }
  }
}

uint32_t FakeAp::GetNumAssociatedClient() const {
  uint32_t client_count = 0;
  for (auto it = clients_.begin(); it != clients_.end(); it++) {
    if ((*it)->status_ == Client::ASSOCIATED) {
      client_count++;
    }
  }
  return client_count;
}

void FakeAp::ScheduleAssocResp(wlan_ieee80211::StatusCode status, const common::MacAddr& dst) {
  environment_->ScheduleNotification(
      std::bind(&FakeAp::HandleAssocRespNotification, this, status, dst), assoc_resp_interval_);
}

void FakeAp::ScheduleReassocResp(::fuchsia::wlan::ieee80211::StatusCode status,
                                 const common::MacAddr& dst) {
  environment_->ScheduleNotification(
      [this, status, dst] { HandleReassocRespNotification(status, dst); }, reassoc_resp_interval_);
}

void FakeAp::ScheduleProbeResp(const common::MacAddr& dst) {
  environment_->ScheduleNotification(std::bind(&FakeAp::HandleProbeRespNotification, this, dst),
                                     probe_resp_interval_);
}

void FakeAp::ScheduleAuthResp(std::shared_ptr<const SimAuthFrame> auth_frame_in,
                              wlan_ieee80211::StatusCode status) {
  SimAuthFrame auth_resp_frame(bssid_, auth_frame_in->src_addr_, auth_frame_in->seq_num_,
                               auth_frame_in->auth_type_, status);
  auth_resp_frame.sec_proto_type_ = security_.sec_type;
  if (security_.auth_handling_mode == AUTH_TYPE_SAE) {
    // Here we copy the SAE payload from the auth req frame to auth resp frame without any
    // modification for test purpose.
    auth_resp_frame.payload_ = auth_frame_in->payload_;
  } else {
    // The seq_num of SAE authentication frame is {1, 1, 2, 2} instead of {1, 2, 3, 4}.
    auth_resp_frame.seq_num_ += 1;
  }

  environment_->ScheduleNotification(
      std::bind(&FakeAp::HandleAuthRespNotification, this, auth_resp_frame), auth_resp_interval_);
}

void FakeAp::ScheduleQosData(bool toDS, bool fromDS, const common::MacAddr& addr1,
                             const common::MacAddr& addr2, const common::MacAddr& addr3,
                             const std::vector<uint8_t>& payload) {
  environment_->ScheduleNotification(std::bind(&FakeAp::HandleQosDataNotification, this, toDS,
                                               fromDS, addr1, addr2, addr3, payload),
                                     data_forward_interval_);
}

void FakeAp::Rx(std::shared_ptr<const SimFrame> frame, std::shared_ptr<const WlanRxInfo> info) {
  // Make sure we heard it
  if (!CanReceiveChannel(info->channel)) {
    return;
  }

  switch (frame->FrameType()) {
    case SimFrame::FRAME_TYPE_MGMT: {
      RxMgmtFrame(std::static_pointer_cast<const SimManagementFrame>(frame));
      break;
    }
    case SimFrame::FRAME_TYPE_DATA: {
      RxDataFrame(std::static_pointer_cast<const SimDataFrame>(frame));
      break;
    }
    default:
      break;
  }
}

void FakeAp::RxMgmtFrame(std::shared_ptr<const SimManagementFrame> mgmt_frame) {
  using wlan_ieee80211::StatusCode;

  switch (mgmt_frame->MgmtFrameType()) {
    // TODO(fxbug.dev/89334): A probe response should only be sent if the Probe Request
    // contains the SSID of this AP.
    case SimManagementFrame::FRAME_TYPE_PROBE_REQ: {
      auto probe_req_frame = std::static_pointer_cast<const SimProbeReqFrame>(mgmt_frame);
      ScheduleProbeResp(probe_req_frame->src_addr_);
      break;
    }

    case SimManagementFrame::FRAME_TYPE_ASSOC_REQ: {
      auto assoc_req_frame = std::static_pointer_cast<const SimAssocReqFrame>(mgmt_frame);
      // Ignore requests that are not for us
      if (assoc_req_frame->bssid_ != bssid_) {
        return;
      }

      if (assoc_handling_mode_ == ASSOC_IGNORED) {
        return;
      }

      if (assoc_handling_mode_ == ASSOC_REFUSED_TEMPORARILY) {
        ScheduleAssocResp(StatusCode::REFUSED_TEMPORARILY, assoc_req_frame->src_addr_);
        return;
      }

      if (assoc_handling_mode_ == ASSOC_REFUSED) {
        ScheduleAssocResp(StatusCode::REFUSED_REASON_UNSPECIFIED, assoc_req_frame->src_addr_);
        return;
      }

      if ((assoc_req_frame->ssid_.len != ssid_.len) ||
          memcmp(assoc_req_frame->ssid_.data, ssid_.data, ssid_.len)) {
        ScheduleAssocResp(StatusCode::REFUSED_REASON_UNSPECIFIED, assoc_req_frame->src_addr_);
        return;
      }

      auto client = FindClient(assoc_req_frame->src_addr_);

      if (!client) {
        ScheduleAssocResp(StatusCode::REFUSED_REASON_UNSPECIFIED, assoc_req_frame->src_addr_);
        return;
      }

      // Make sure the client is not associated.
      if (client->status_ == Client::ASSOCIATED) {
        ScheduleAssocResp(StatusCode::REFUSED_TEMPORARILY, assoc_req_frame->src_addr_);
        return;
      }

      if (client->status_ != Client::AUTHENTICATED) {
        // If the status of this client is AUTHENTICATING, we also remove it from the list.
        RemoveClient(assoc_req_frame->src_addr_);
        ScheduleAssocResp(StatusCode::REFUSED_REASON_UNSPECIFIED, assoc_req_frame->src_addr_);
        return;
      }

      client->status_ = Client::ASSOCIATED;
      ScheduleAssocResp(StatusCode::SUCCESS, assoc_req_frame->src_addr_);
      break;
    }

    case SimManagementFrame::FRAME_TYPE_DISASSOC_REQ: {
      auto disassoc_req_frame = std::static_pointer_cast<const SimDisassocReqFrame>(mgmt_frame);
      // Ignore requests that are not for us
      if (disassoc_req_frame->dst_addr_ != bssid_) {
        return;
      }

      // Make sure the client is already associated
      for (auto client : clients_) {
        if (client->mac_addr_ == disassoc_req_frame->src_addr_ &&
            client->status_ == Client::ASSOCIATED) {
          // Client is already associated
          RemoveClient(disassoc_req_frame->src_addr_);
          return;
        }
      }
      break;
    }

    case SimManagementFrame::FRAME_TYPE_AUTH: {
      auto auth_req_frame = std::static_pointer_cast<const SimAuthFrame>(mgmt_frame);
      if (auth_req_frame->dst_addr_ != bssid_) {
        return;
      }

      if (assoc_handling_mode_ == ASSOC_IGNORED) {
        return;
      }

      if (security_.sec_type != auth_req_frame->sec_proto_type_) {
        ScheduleAuthResp(auth_req_frame, StatusCode::REFUSED_REASON_UNSPECIFIED);
        return;
      }

      // If it's not matching AP's authentication handling mode, just reply a refuse.
      if (auth_req_frame->auth_type_ != security_.auth_handling_mode) {
        RemoveClient(auth_req_frame->src_addr_);
        ScheduleAuthResp(auth_req_frame, StatusCode::REFUSED_REASON_UNSPECIFIED);
        return;
      }

      // Filt out frames in which sec_type does not match auth_type.
      if ((security_.sec_type == SEC_PROTO_TYPE_WPA1 ||
           security_.sec_type == SEC_PROTO_TYPE_WPA2) &&
          auth_req_frame->auth_type_ != AUTH_TYPE_OPEN) {
        ScheduleAuthResp(auth_req_frame, StatusCode::REFUSED_REASON_UNSPECIFIED);
        return;
      }

      if (security_.sec_type == SEC_PROTO_TYPE_WPA3 &&
          auth_req_frame->auth_type_ != AUTH_TYPE_SAE) {
        ScheduleAuthResp(auth_req_frame, StatusCode::REFUSED_REASON_UNSPECIFIED);
        return;
      }

      // Filt out frames in which seq_num does not match auth_type.
      if (auth_req_frame->seq_num_ != 1 &&
          (auth_req_frame->seq_num_ != 3 || auth_req_frame->auth_type_ == AUTH_TYPE_OPEN) &&
          (auth_req_frame->seq_num_ != 2 || auth_req_frame->auth_type_ != AUTH_TYPE_SAE)) {
        RemoveClient(auth_req_frame->src_addr_);
        return;
      }

      // Status of auth req should be WLAN_STATUS_CODE_SUCCESS
      if (auth_req_frame->status_ != StatusCode::SUCCESS) {
        RemoveClient(auth_req_frame->src_addr_);
        return;
      }

      // Remove the client and start the authentication and association process again if it is
      // already connected.
      auto client = FindClient(auth_req_frame->src_addr_);
      if (client && client->status_ == Client::ASSOCIATED) {
        RemoveClient(auth_req_frame->src_addr_);
        client = nullptr;
      }

      if (!client) {
        // A new client need to conduct authentication
        if (auth_req_frame->seq_num_ != 1)
          return;
        client = AddClient(auth_req_frame->src_addr_);
      }

      switch (security_.auth_handling_mode) {
        case AUTH_TYPE_OPEN:
          // Even when the client status is AUTHENTICATED, we will send out a auth resp frame to let
          // client's status catch up in a same pace as AP.
          client->status_ = Client::AUTHENTICATED;
          break;
        case AUTH_TYPE_SHARED_KEY:
          if (client->status_ == Client::NOT_AUTHENTICATED) {
            // We've already checked whether the seq_num is 1.
            client->status_ = Client::AUTHENTICATING;
          } else if (client->status_ == Client::AUTHENTICATING) {
            if (auth_req_frame->seq_num_ == 3) {
              if (security_.expect_challenge_failure) {
                // Refuse authentication if this AP has been configured to.
                // TODO (fxb/61139): Actually check the challenge response rather than hardcoding
                // authentication success or failure using expect_challenge_failure.
                RemoveClient(auth_req_frame->src_addr_);
                ScheduleAuthResp(auth_req_frame, StatusCode::CHALLENGE_FAILURE);
                return;
              }

              client->status_ = Client::AUTHENTICATED;
            }
            // If the seq num is 1, we will just send out a resp and keep the status.
          }
          // If the status is already AUTHENTICATED, we will just send out a resp and keep the
          // status.
          break;
        case AUTH_TYPE_SAE:
          if (client->status_ == Client::NOT_AUTHENTICATED) {
            // We've already checked whether the seq_num is 1.
            client->status_ = Client::AUTHENTICATING;
          } else if (client->status_ == Client::AUTHENTICATING) {
            if (auth_req_frame->seq_num_ == 2) {
              client->status_ = Client::AUTHENTICATED;
            }
          }

          break;
        default:
          ZX_ASSERT_MSG(false, "Unsupported auth_handling_mode.");
      }

      ScheduleAuthResp(auth_req_frame, StatusCode::SUCCESS);
      break;
    }

    case SimManagementFrame::FRAME_TYPE_REASSOC_REQ: {
      auto reassoc_req_frame = std::static_pointer_cast<const SimReassocReqFrame>(mgmt_frame);
      // Ignore requests that are not for us
      if (reassoc_req_frame->bssid_ != bssid_) {
        return;
      }

      // Use same handling modes as assoc, until (if) we need something fancier.
      if (assoc_handling_mode_ == ASSOC_IGNORED) {
        return;
      }

      if (assoc_handling_mode_ == ASSOC_REFUSED_TEMPORARILY) {
        ScheduleReassocResp(StatusCode::REFUSED_TEMPORARILY, reassoc_req_frame->src_addr_);
        return;
      }

      if (assoc_handling_mode_ == ASSOC_REFUSED) {
        ScheduleReassocResp(StatusCode::REFUSED_REASON_UNSPECIFIED, reassoc_req_frame->src_addr_);
        return;
      }

      auto client = FindClient(reassoc_req_frame->src_addr_);
      if (!client) {
        client = AddClient(reassoc_req_frame->src_addr_);
        // Add the client in initial state.
        client->status_ = Client::NOT_AUTHENTICATED;
      }
      // We only test the happy reassociation path now, so skip right to ASSOCIATED.
      // When we test the other reassociation paths, this is where we might differentiate which
      // state the STA lands in.
      client->status_ = Client::ASSOCIATED;
      ScheduleReassocResp(StatusCode::SUCCESS, reassoc_req_frame->src_addr_);
      break;
    }

    default:
      break;
  }
}

void FakeAp::RxDataFrame(std::shared_ptr<const SimDataFrame> data_frame) {
  switch (data_frame->DataFrameType()) {
    case SimDataFrame::FRAME_TYPE_QOS_DATA:
      // If we are not the intended receiver, ignore it
      if (data_frame->addr1_ != bssid_) {
        return;
      }

      // IEEE Std 802.11-2016, 9.2.4.1.4
      if (data_frame->toDS_ && data_frame->fromDS_) {
        ZX_ASSERT_MSG(false, "No support for Mesh data frames in Fake AP\n");
      } else if (data_frame->toDS_ && !data_frame->fromDS_) {
        // Currently no sim support for any PAE and any higher level protocols, so just check other
        // local clients in infrastructure BSS, otherwise don't deliver anything
        for (auto client : clients_) {
          if (data_frame->addr3_ == client->mac_addr_) {
            // Forward frame to destination
            ScheduleQosData(false, true, client->mac_addr_, bssid_, data_frame->addr2_,
                            data_frame->payload_);
            break;
          }
        }
      } else {
        // Under the assumption that the Fake AP does not support Mesh data frames
        ZX_ASSERT_MSG(false,
                      "Data frame addressed to AP but marked destination as STA, frame invalid\n");
      }
      break;
    default:
      break;
  }
}

zx_status_t FakeAp::DisassocSta(const common::MacAddr& sta_mac, wlan_ieee80211::ReasonCode reason) {
  // Make sure the client is already associated
  SimDisassocReqFrame disassoc_req_frame(bssid_, sta_mac, reason);
  for (auto client : clients_) {
    if (client->mac_addr_ == sta_mac && client->status_ == Client::ASSOCIATED) {
      // Client is already associated
      environment_->Tx(disassoc_req_frame, tx_info_, this);
      RemoveClient(sta_mac);
      return ZX_OK;
    }
  }
  // client not found
  return ZX_ERR_INVALID_ARGS;
}

void FakeAp::HandleBeaconNotification() {
  ZX_ASSERT(beacon_state_.is_beaconing);
  SimBeaconFrame tmp_beacon_frame(beacon_state_.beacon_frame_);
  if (CheckIfErrInjBeaconEnabled()) {
    tmp_beacon_frame = beacon_state_.beacon_mutator(tmp_beacon_frame);
  }
  environment_->Tx(tmp_beacon_frame, tx_info_, this);
  // Channel switch count decrease by 1 each time after sending a CSA beacon.
  if (beacon_state_.is_switching_channel) {
    auto csa_generic_ie = beacon_state_.beacon_frame_.FindIe(InformationElement::IE_TYPE_CSA);
    ZX_ASSERT(csa_generic_ie != nullptr);
    auto csa_ie = std::static_pointer_cast<CsaInformationElement>(csa_generic_ie);
    ZX_ASSERT(csa_ie->channel_switch_count_ > 0);
    csa_ie->channel_switch_count_--;
  }
  ScheduleNextBeacon();
}

void FakeAp::AddErrInjBeacon(std::function<SimBeaconFrame(const SimBeaconFrame&)> beacon_mutator) {
  beacon_state_.beacon_mutator = std::move(beacon_mutator);
}

void FakeAp::DelErrInjBeacon() { beacon_state_.beacon_mutator = nullptr; }

bool FakeAp::CheckIfErrInjBeaconEnabled() const { return beacon_state_.beacon_mutator != nullptr; }

void FakeAp::HandleStopCsaBeaconNotification() {
  ZX_ASSERT(beacon_state_.is_beaconing);
  beacon_state_.beacon_frame_.RemoveIe(InformationElement::IE_TYPE_CSA);
  tx_info_.channel = beacon_state_.channel_after_csa;
  beacon_state_.is_switching_channel = false;
}

void FakeAp::HandleAssocRespNotification(wlan_ieee80211::StatusCode status, common::MacAddr dst) {
  SimAssocRespFrame assoc_resp_frame(bssid_, dst, status);
  assoc_resp_frame.capability_info_.set_val(beacon_state_.beacon_frame_.capability_info_.val());
  environment_->Tx(assoc_resp_frame, tx_info_, this);
}

void FakeAp::HandleReassocRespNotification(::fuchsia::wlan::ieee80211::StatusCode status,
                                           common::MacAddr dst) {
  SimReassocRespFrame reassoc_resp_frame(bssid_, dst, status);
  reassoc_resp_frame.capability_info_.set_val(beacon_state_.beacon_frame_.capability_info_.val());
  environment_->Tx(reassoc_resp_frame, tx_info_, this);
}

void FakeAp::HandleProbeRespNotification(common::MacAddr dst) {
  SimProbeRespFrame probe_resp_frame(bssid_, dst, ssid_);
  probe_resp_frame.capability_info_.set_val(beacon_state_.beacon_frame_.capability_info_.val());

  environment_->Tx(probe_resp_frame, tx_info_, this);
}

void FakeAp::HandleAuthRespNotification(SimAuthFrame auth_resp_frame) {
  environment_->Tx(auth_resp_frame, tx_info_, this);
}

void FakeAp::HandleQosDataNotification(bool toDS, bool fromDS, const common::MacAddr& addr1,
                                       const common::MacAddr& addr2, const common::MacAddr& addr3,
                                       const std::vector<uint8_t>& payload) {
  SimQosDataFrame data_frame(toDS, fromDS, addr1, addr2, addr3, 0, payload);
  environment_->Tx(data_frame, tx_info_, this);
}

void FakeAp::SetAssocHandling(enum AssocHandling mode) { assoc_handling_mode_ = mode; }

}  // namespace wlan::simulation
