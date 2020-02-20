// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-frame.h"

namespace wlan::simulation {

InformationElement::~InformationElement() = default;
InformationElement::SimIEType CSAInformationElement::IEType() const { return IE_TYPE_CSA; }

CSAInformationElement::~CSAInformationElement() = default;

SimFrame::~SimFrame() = default;

SimManagementFrame::~SimManagementFrame() = default;

SimFrame::SimFrameType SimManagementFrame::FrameType() const { return FRAME_TYPE_MGMT; }

std::shared_ptr<InformationElement> SimManagementFrame::FindIE(
    InformationElement::SimIEType ie_type) const {
  for (auto it = IEs_.begin(); it != IEs_.end(); it++) {
    if ((*it)->IEType() == ie_type) {
      return *it;
    }
  }

  return std::shared_ptr<InformationElement>(nullptr);
}

void SimManagementFrame::AddCSAIE(const wlan_channel_t& channel, uint8_t channel_switch_count) {
  // for nonmesh STAs, this field either is set to the number of TBTTs until the STA sending the
  // Channel Switch Announcement element switches to the new channel or is set to 0.
  auto ie = std::make_shared<CSAInformationElement>(false, channel.primary, channel_switch_count);
  // Ensure no IE with this IE type exist
  AddIE(InformationElement::IE_TYPE_CSA, ie);
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

SimBeaconFrame::~SimBeaconFrame() = default;
SimManagementFrame::SimMgmtFrameType SimBeaconFrame::MgmtFrameType() const {
  return FRAME_TYPE_BEACON;
}

SimProbeReqFrame::~SimProbeReqFrame() = default;
SimManagementFrame::SimMgmtFrameType SimProbeReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_PROBE_REQ;
}

SimProbeRespFrame::~SimProbeRespFrame() = default;
SimManagementFrame::SimMgmtFrameType SimProbeRespFrame::MgmtFrameType() const {
  return FRAME_TYPE_PROBE_RESP;
}

SimAssocReqFrame::~SimAssocReqFrame() = default;
SimManagementFrame::SimMgmtFrameType SimAssocReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_ASSOC_REQ;
}

SimAssocRespFrame::~SimAssocRespFrame() = default;
SimManagementFrame::SimMgmtFrameType SimAssocRespFrame::MgmtFrameType() const {
  return FRAME_TYPE_ASSOC_RESP;
}

SimDisassocReqFrame::~SimDisassocReqFrame() = default;
SimManagementFrame::SimMgmtFrameType SimDisassocReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_DISASSOC_REQ;
}

SimAuthFrame::~SimAuthFrame() {}
SimManagementFrame::SimMgmtFrameType SimAuthFrame::MgmtFrameType() const { return FRAME_TYPE_AUTH; }

}  // namespace wlan::simulation
