// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-frame.h"

namespace wlan::simulation {

SimFrame::~SimFrame() {}

SimManagementFrame::~SimManagementFrame() {}
SimFrame::SimFrameType SimManagementFrame::FrameType() const { return FRAME_TYPE_MGMT; }

SimBeaconFrame::~SimBeaconFrame() {}
SimManagementFrame::SimMgmtFrameType SimBeaconFrame::MgmtFrameType() const {
  return FRAME_TYPE_BEACON;
}

SimProbeReqFrame::~SimProbeReqFrame() {}
SimManagementFrame::SimMgmtFrameType SimProbeReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_PROBE_REQ;
}

SimProbeRespFrame::~SimProbeRespFrame() {}
SimManagementFrame::SimMgmtFrameType SimProbeRespFrame::MgmtFrameType() const {
  return FRAME_TYPE_PROBE_RESP;
}

SimAssocReqFrame::~SimAssocReqFrame() {}
SimManagementFrame::SimMgmtFrameType SimAssocReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_ASSOC_REQ;
}

SimAssocRespFrame::~SimAssocRespFrame() {}
SimManagementFrame::SimMgmtFrameType SimAssocRespFrame::MgmtFrameType() const {
  return FRAME_TYPE_ASSOC_RESP;
}

SimDisassocReqFrame::~SimDisassocReqFrame() {}
SimManagementFrame::SimMgmtFrameType SimDisassocReqFrame::MgmtFrameType() const {
  return FRAME_TYPE_DISASSOC_REQ;
}

}  // namespace wlan::simulation
