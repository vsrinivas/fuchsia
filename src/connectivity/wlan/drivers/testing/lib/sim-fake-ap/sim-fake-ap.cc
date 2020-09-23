// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-fake-ap.h"

#include <zircon/assert.h>

#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::simulation {

void FakeAp::SetChannel(const wlan_channel_t& channel) {
  // Time until next beacon.
  zx::duration diff_to_next_beacon = beacon_state_.next_beacon_time - environment_->GetTime();

  // If any station is associating with this AP, trigger channel switch.
  if (GetNumAssociatedClient() > 0 && beacon_state_.is_beaconing &&
      (CSA_beacon_interval_ >= diff_to_next_beacon)) {
    // If a new CSA is triggered, then it will override the previous one, and schedule a new channel
    // switch time.
    uint8_t cs_count = 0;
    // This is the time period start from next beacon to the end of CSA beacon interval.
    zx::duration cover = CSA_beacon_interval_ - diff_to_next_beacon;
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

    beacon_state_.beacon_frame_.AddCSAIE(channel, cs_count);
    beacon_state_.channel_after_CSA = channel;

    auto stop_CSAbeacon_handler = std::make_unique<std::function<void()>>();
    *stop_CSAbeacon_handler = std::bind(&FakeAp::HandleStopCSABeaconNotification, this);
    environment_->ScheduleNotification(std::move(stop_CSAbeacon_handler), CSA_beacon_interval_,
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

void FakeAp::SetSsid(const wlan_ssid_t& ssid) {
  ssid_ = ssid;
  beacon_state_.beacon_frame_.AddSSIDIE(ssid);
}

void FakeAp::SetCSABeaconInterval(zx::duration interval) {
  // Meaningless to set CSA_beacon_interval to 0.
  ZX_ASSERT(interval.get() != 0);
  CSA_beacon_interval_ = interval;
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
  auto beacon_handler = std::make_unique<std::function<void()>>();
  *beacon_handler = std::bind(&FakeAp::HandleBeaconNotification, this);
  environment_->ScheduleNotification(std::move(beacon_handler),
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
    tx_info_.channel = beacon_state_.channel_after_CSA;
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

void FakeAp::ScheduleAssocResp(uint16_t status, const common::MacAddr& dst) {
  auto handler = std::make_unique<std::function<void()>>();
  *handler = std::bind(&FakeAp::HandleAssocRespNotification, this, status, dst);
  environment_->ScheduleNotification(std::move(handler), assoc_resp_interval_);
}

void FakeAp::ScheduleProbeResp(const common::MacAddr& dst) {
  auto handler = std::make_unique<std::function<void()>>();
  *handler = std::bind(&FakeAp::HandleProbeRespNotification, this, dst);
  environment_->ScheduleNotification(std::move(handler), probe_resp_interval_);
}

void FakeAp::ScheduleAuthResp(uint16_t seq_num_in, const common::MacAddr& dst,
                              SimAuthType auth_type, uint16_t status) {
  auto handler = std::make_unique<std::function<void()>>();
  *handler =
      std::bind(&FakeAp::HandleAuthRespNotification, this, seq_num_in + 1, dst, auth_type, status);
  environment_->ScheduleNotification(std::move(handler), auth_resp_interval_);
}

void FakeAp::ScheduleQosData(bool toDS, bool fromDS, const common::MacAddr& addr1,
                             const common::MacAddr& addr2, const common::MacAddr& addr3,
                             const std::vector<uint8_t>& payload) {
  auto handler = std::make_unique<std::function<void()>>();
  *handler = std::bind(&FakeAp::HandleQosDataNotification, this, toDS, fromDS, addr1, addr2, addr3,
                       payload);
  environment_->ScheduleNotification(std::move(handler), data_forward_interval_);
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
  switch (mgmt_frame->MgmtFrameType()) {
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

      if ((assoc_req_frame->ssid_.len != ssid_.len) ||
          memcmp(assoc_req_frame->ssid_.ssid, ssid_.ssid, ssid_.len)) {
        ScheduleAssocResp(WLAN_STATUS_CODE_REFUSED, assoc_req_frame->src_addr_);
        return;
      }

      if (assoc_handling_mode_ == ASSOC_REJECTED) {
        ScheduleAssocResp(WLAN_STATUS_CODE_REFUSED, assoc_req_frame->src_addr_);
        return;
      }

      auto client = FindClient(assoc_req_frame->src_addr_);

      if (!client) {
        ScheduleAssocResp(WLAN_STATUS_CODE_REFUSED, assoc_req_frame->src_addr_);
        return;
      }

      // Make sure the client is not associated.
      if (client->status_ == Client::ASSOCIATED) {
        ScheduleAssocResp(WLAN_STATUS_CODE_REFUSED_TEMPORARILY, assoc_req_frame->src_addr_);
        return;
      }

      if (client->status_ != Client::AUTHENTICATED) {
        // If the status of this client is AUTHENTICATING, we also remove it from the list.
        RemoveClient(assoc_req_frame->src_addr_);
        ScheduleAssocResp(WLAN_STATUS_CODE_REFUSED, assoc_req_frame->src_addr_);
        return;
      }

      client->status_ = Client::ASSOCIATED;
      ScheduleAssocResp(WLAN_STATUS_CODE_SUCCESS, assoc_req_frame->src_addr_);
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

      if (assoc_handling_mode_ == ASSOC_REJECTED) {
        ScheduleAuthResp(auth_req_frame->seq_num_, auth_req_frame->src_addr_,
                         auth_req_frame->auth_type_, WLAN_STATUS_CODE_REFUSED);
        return;
      }

      if (security_.sec_type != auth_req_frame->sec_proto_type_) {
        ScheduleAuthResp(auth_req_frame->seq_num_, auth_req_frame->src_addr_,
                         auth_req_frame->auth_type_, WLAN_STATUS_CODE_REFUSED);
        return;
      }

      if ((security_.sec_type == SEC_PROTO_TYPE_WPA1 ||
           security_.sec_type == SEC_PROTO_TYPE_WPA2) &&
          auth_req_frame->auth_type_ == AUTH_TYPE_SHARED_KEY) {
        ScheduleAuthResp(auth_req_frame->seq_num_, auth_req_frame->src_addr_,
                         auth_req_frame->auth_type_, WLAN_STATUS_CODE_REFUSED);
        return;
      }

      // Filt out invalid authentication request frames.
      if (auth_req_frame->seq_num_ != 1 &&
          (auth_req_frame->seq_num_ != 3 || auth_req_frame->auth_type_ == AUTH_TYPE_OPEN)) {
        RemoveClient(auth_req_frame->src_addr_);
        return;
      }

      // Status of auth req should be WLAN_STATUS_CODE_SUCCESS
      if (auth_req_frame->status_ != WLAN_STATUS_CODE_SUCCESS) {
        RemoveClient(auth_req_frame->src_addr_);
        return;
      }

      // If it's not matching AP's authentication handling mode, just reply a refuse.
      if (auth_req_frame->auth_type_ != security_.auth_handling_mode) {
        RemoveClient(auth_req_frame->src_addr_);
        ScheduleAuthResp(auth_req_frame->seq_num_, auth_req_frame->src_addr_,
                         auth_req_frame->auth_type_, WLAN_STATUS_CODE_REFUSED);
        return;
      }

      // Make sure this client is not associated to continue authentication
      auto client = FindClient(auth_req_frame->src_addr_);
      if (client && client->status_ == Client::ASSOCIATED) {
        return;
      }

      if (!client) {
        // A new client need to conduct authentication
        if (auth_req_frame->seq_num_ != 1)
          return;
        client = AddClient(auth_req_frame->src_addr_);
      }

      if (security_.auth_handling_mode == AUTH_TYPE_OPEN) {
        // Even when the client status is AUTHENTICATED, we will send out a auth resp frame to let
        // client's status catch up in a same pace as AP.
        client->status_ = Client::AUTHENTICATED;
      } else if (security_.auth_handling_mode == AUTH_TYPE_SHARED_KEY) {
        if (client->status_ == Client::NOT_AUTHENTICATED) {
          // We've already checked whether the seq_num is 1.
          client->status_ = Client::AUTHENTICATING;
        } else if (client->status_ == Client::AUTHENTICATING) {
          if (auth_req_frame->seq_num_ == 3)
            client->status_ = Client::AUTHENTICATED;
          // If the seq num is 1, we will just send out a resp and keep the status.
        }
        // If the status is already AUTHENTICATED, we will just send out a resp and keep the status.
      }

      ScheduleAuthResp(auth_req_frame->seq_num_, auth_req_frame->src_addr_,
                       auth_req_frame->auth_type_, WLAN_STATUS_CODE_SUCCESS);
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

zx_status_t FakeAp::DisassocSta(const common::MacAddr& sta_mac, uint16_t reason) {
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
  environment_->Tx(tmp_beacon_frame, tx_info_, this);
  // Channel switch count decrease by 1 each time after sending a CSA beacon.
  if (beacon_state_.is_switching_channel) {
    auto CSA_ie = beacon_state_.beacon_frame_.FindIE(InformationElement::IE_TYPE_CSA);
    ZX_ASSERT(static_cast<CSAInformationElement*>(CSA_ie.get())->channel_switch_count_-- > 0);
  }
  ScheduleNextBeacon();
}

void FakeAp::HandleStopCSABeaconNotification() {
  ZX_ASSERT(beacon_state_.is_beaconing);
  beacon_state_.beacon_frame_.RemoveIE(InformationElement::SimIEType::IE_TYPE_CSA);
  tx_info_.channel = beacon_state_.channel_after_CSA;
  beacon_state_.is_switching_channel = false;
}

void FakeAp::HandleAssocRespNotification(uint16_t status, common::MacAddr dst) {
  SimAssocRespFrame assoc_resp_frame(bssid_, dst, status);
  assoc_resp_frame.capability_info_.set_val(beacon_state_.beacon_frame_.capability_info_.val());
  environment_->Tx(assoc_resp_frame, tx_info_, this);
}

void FakeAp::HandleProbeRespNotification(common::MacAddr dst) {
  SimProbeRespFrame probe_resp_frame(bssid_, dst, ssid_);
  probe_resp_frame.capability_info_.set_val(beacon_state_.beacon_frame_.capability_info_.val());

  environment_->Tx(probe_resp_frame, tx_info_, this);
}

void FakeAp::HandleAuthRespNotification(uint16_t seq_num, common::MacAddr dst,
                                        SimAuthType auth_type, uint16_t status) {
  SimAuthFrame auth_resp_frame(bssid_, dst, seq_num, auth_type, status);
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
