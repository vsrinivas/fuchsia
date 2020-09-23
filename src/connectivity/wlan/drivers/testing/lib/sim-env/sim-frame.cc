// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-frame.h"

#include "fbl/span.h"

namespace wlan::simulation {

/* InformationElement function implementations.*/
InformationElement::~InformationElement() = default;

// SSIDInformationElement function implementations.
SSIDInformationElement::SSIDInformationElement(
    const wlan::simulation::SSIDInformationElement& ssid_ie) {
  ssid_.len = ssid_ie.ssid_.len;
  std::memcpy(ssid_.ssid, ssid_ie.ssid_.ssid, ssid_.len);
};

InformationElement::SimIEType SSIDInformationElement::IEType() const { return IE_TYPE_SSID; }

std::vector<uint8_t> SSIDInformationElement::ToRawIe() const {
  std::vector<uint8_t> buf = {IE_TYPE_SSID, ssid_.len};
  for (int i = 0; i < ssid_.len; ++i) {
    buf.push_back(ssid_.ssid[i]);
  }
  return buf;
}

SSIDInformationElement::~SSIDInformationElement() = default;

// CSAInformationElement function implementations.
CSAInformationElement::CSAInformationElement(
    const wlan::simulation::CSAInformationElement& csa_ie) {
  channel_switch_mode_ = csa_ie.channel_switch_mode_;
  new_channel_number_ = csa_ie.new_channel_number_;
  channel_switch_count_ = csa_ie.channel_switch_count_;
}

InformationElement::SimIEType CSAInformationElement::IEType() const { return IE_TYPE_CSA; }

std::vector<uint8_t> CSAInformationElement::ToRawIe() const {
  const uint8_t csa_len = 3;  // CSA variable is 3 bytes long: CSM + NCN + CSC.
  std::vector<uint8_t> buf = {IE_TYPE_CSA, csa_len,
                              static_cast<uint8_t>(channel_switch_mode_ ? 1 : 0),
                              new_channel_number_, channel_switch_count_};
  return buf;
}

CSAInformationElement::~CSAInformationElement() = default;

/* SimFrame function implementations.*/
SimFrame::~SimFrame() = default;

/* SimManagementFrame function implementations.*/
SimManagementFrame::SimManagementFrame(const SimManagementFrame& mgmt_frame) {
  src_addr_ = mgmt_frame.src_addr_;
  dst_addr_ = mgmt_frame.dst_addr_;
  sec_proto_type_ = mgmt_frame.sec_proto_type_;

  for (const auto& ie : mgmt_frame.IEs_) {
    switch (ie->IEType()) {
      case SSIDInformationElement::IE_TYPE_SSID:
        IEs_.push_back(std::make_shared<SSIDInformationElement>(
            *(std::static_pointer_cast<SSIDInformationElement>(ie))));
        break;
      case CSAInformationElement::IE_TYPE_CSA:
        IEs_.push_back(std::make_shared<CSAInformationElement>(
            *(std::static_pointer_cast<CSAInformationElement>(ie))));
        break;
      default:;
    }
  }
  raw_ies_ = mgmt_frame.raw_ies_;
}

SimManagementFrame::~SimManagementFrame() = default;

SimFrame::SimFrameType SimManagementFrame::FrameType() const { return FRAME_TYPE_MGMT; }

std::shared_ptr<InformationElement> SimManagementFrame::FindIE(
    InformationElement::SimIEType ie_type) const {
  for (const auto& ie : IEs_) {
    if (ie->IEType() == ie_type) {
      return ie;
    }
  }

  return std::shared_ptr<InformationElement>(nullptr);
}

void SimManagementFrame::AddSSIDIE(const wlan_ssid_t& ssid) {
  auto ie = std::make_shared<SSIDInformationElement>(ssid);
  // Ensure no IE with this IE type exists.
  AddIE(InformationElement::IE_TYPE_SSID, ie);
}

void SimManagementFrame::AddCSAIE(const wlan_channel_t& channel, uint8_t channel_switch_count) {
  // for nonmesh STAs, this field either is set to the number of TBTTs until the STA sending the
  // Channel Switch Announcement element switches to the new channel or is set to 0.
  auto ie = std::make_shared<CSAInformationElement>(false, channel.primary, channel_switch_count);
  // Ensure no IE with this IE type exist
  AddIE(InformationElement::IE_TYPE_CSA, ie);
}

void SimManagementFrame::AddRawIes(fbl::Span<const uint8_t> raw_ies) {
  raw_ies_.insert(raw_ies_.end(), raw_ies.begin(), raw_ies.end());
}

void SimManagementFrame::AddIE(InformationElement::SimIEType ie_type,
                               std::shared_ptr<InformationElement> ie) {
  if (FindIE(ie_type)) {
    RemoveIE(ie_type);
  }
  IEs_.push_back(ie);
}

void SimManagementFrame::RemoveIE(InformationElement::SimIEType ie_type) {
  for (auto it = IEs_.begin(); it != IEs_.end();) {
    if ((*it)->IEType() == ie_type) {
      it = IEs_.erase(it);
    } else {
      it++;
    }
  }
}

/* SimBeaconFrame function implementations.*/
SimBeaconFrame::SimBeaconFrame(const wlan_ssid_t& ssid, const common::MacAddr& bssid)
    : bssid_(bssid) {
  // Beacon automatically gets the SSID information element.
  AddSSIDIE(ssid);
}

SimBeaconFrame::SimBeaconFrame(const SimBeaconFrame& beacon) : SimManagementFrame(beacon) {
  bssid_ = beacon.bssid_;
  interval_ = beacon.interval_;
  capability_info_ = beacon.capability_info_;
  // IEs are copied by SimManagementFrame copy constructor.
}

SimBeaconFrame::~SimBeaconFrame() = default;
SimManagementFrame::SimMgmtFrameType SimBeaconFrame::MgmtFrameType() const {
  return FRAME_TYPE_BEACON;
}

SimFrame* SimBeaconFrame::CopyFrame() const { return new SimBeaconFrame(*this); }

/* SimProbeReqFrame function implementations.*/
SimProbeReqFrame::SimProbeReqFrame(const SimProbeReqFrame& probe_req)
    : SimManagementFrame(probe_req) {}

SimProbeReqFrame::~SimProbeReqFrame() = default;
SimManagementFrame::SimMgmtFrameType SimProbeReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_PROBE_REQ;
}

SimFrame* SimProbeReqFrame::CopyFrame() const { return new SimProbeReqFrame(*this); }

/* SimProbeRespFrame function implementations.*/
SimProbeRespFrame::SimProbeRespFrame(const common::MacAddr& src, const common::MacAddr& dst,
                                     const wlan_ssid_t& ssid)
    : SimManagementFrame(src, dst) {
  // Probe response automatically gets the SSID information element.
  AddSSIDIE(ssid);
}

SimProbeRespFrame::SimProbeRespFrame(const SimProbeRespFrame& probe_resp)
    : SimManagementFrame(probe_resp) {
  capability_info_ = probe_resp.capability_info_;
  // IEs are copied by SimManagementFrame copy constructor.
}

SimProbeRespFrame::~SimProbeRespFrame() = default;
SimManagementFrame::SimMgmtFrameType SimProbeRespFrame::MgmtFrameType() const {
  return FRAME_TYPE_PROBE_RESP;
}

SimFrame* SimProbeRespFrame::CopyFrame() const { return new SimProbeRespFrame(*this); }

/* SimAssocReqFrame function implementations.*/
SimAssocReqFrame::SimAssocReqFrame(const SimAssocReqFrame& assoc_req)
    : SimManagementFrame(assoc_req) {
  bssid_ = assoc_req.bssid_;
  ssid_ = assoc_req.ssid_;
}

SimAssocReqFrame::~SimAssocReqFrame() = default;
SimManagementFrame::SimMgmtFrameType SimAssocReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_ASSOC_REQ;
}

SimFrame* SimAssocReqFrame::CopyFrame() const { return new SimAssocReqFrame(*this); }

/* SimAssocRespFrame function implementations.*/
SimAssocRespFrame::SimAssocRespFrame(const SimAssocRespFrame& assoc_resp)
    : SimManagementFrame(assoc_resp) {
  status_ = assoc_resp.status_;
  capability_info_ = assoc_resp.capability_info_;
}

SimAssocRespFrame::~SimAssocRespFrame() = default;
SimManagementFrame::SimMgmtFrameType SimAssocRespFrame::MgmtFrameType() const {
  return FRAME_TYPE_ASSOC_RESP;
}

SimFrame* SimAssocRespFrame::CopyFrame() const { return new SimAssocRespFrame(*this); }

/* SimDisassocReqFrame function implementations.*/
SimDisassocReqFrame::SimDisassocReqFrame(const SimDisassocReqFrame& disassoc_req)
    : SimManagementFrame(disassoc_req) {
  reason_ = disassoc_req.reason_;
}

SimDisassocReqFrame::~SimDisassocReqFrame() = default;
SimManagementFrame::SimMgmtFrameType SimDisassocReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_DISASSOC_REQ;
}

SimFrame* SimDisassocReqFrame::CopyFrame() const { return new SimDisassocReqFrame(*this); }

/* SimAuthFrame function implementations.*/
SimAuthFrame::SimAuthFrame(const SimAuthFrame& auth) : SimManagementFrame(auth) {
  seq_num_ = auth.seq_num_;
  auth_type_ = auth.auth_type_;
  status_ = auth.status_;
}

SimAuthFrame::~SimAuthFrame() = default;
SimManagementFrame::SimMgmtFrameType SimAuthFrame::MgmtFrameType() const { return FRAME_TYPE_AUTH; }

SimFrame* SimAuthFrame::CopyFrame() const { return new SimAuthFrame(*this); }

/* SimDeauthFrame function implementations.*/
SimDeauthFrame::SimDeauthFrame(const SimDeauthFrame& deauth) : SimManagementFrame(deauth) {
  reason_ = deauth.reason_;
}

SimDeauthFrame::~SimDeauthFrame() = default;
SimManagementFrame::SimMgmtFrameType SimDeauthFrame::MgmtFrameType() const {
  return FRAME_TYPE_DEAUTH;
}

SimFrame* SimDeauthFrame::CopyFrame() const { return new SimDeauthFrame(*this); }

/* SimDataFrame function implementations.*/
SimDataFrame::SimDataFrame(const SimDataFrame& data_frame) {
  toDS_ = data_frame.toDS_;
  fromDS_ = data_frame.fromDS_;
  addr1_ = data_frame.addr1_;
  addr2_ = data_frame.addr2_;
  addr3_ = data_frame.addr3_;
  addr4_ = data_frame.addr4_;
  qosControl_ = data_frame.qosControl_;
  payload_.assign(data_frame.payload_.begin(), data_frame.payload_.end());
}

SimDataFrame::~SimDataFrame() = default;
SimFrame::SimFrameType SimDataFrame::FrameType() const { return FRAME_TYPE_DATA; }

/* SimQosDataFrame function implementations.*/
SimQosDataFrame::SimQosDataFrame(const SimQosDataFrame& qos_data) : SimDataFrame(qos_data) {}

SimQosDataFrame::~SimQosDataFrame() = default;
SimDataFrame::SimDataFrameType SimQosDataFrame::DataFrameType() const {
  return FRAME_TYPE_QOS_DATA;
}

SimFrame* SimQosDataFrame::CopyFrame() const { return new SimQosDataFrame(*this); }

}  // namespace wlan::simulation
