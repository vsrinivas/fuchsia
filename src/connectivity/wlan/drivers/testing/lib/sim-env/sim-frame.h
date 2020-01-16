// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_

#include <zircon/types.h>

#include "sim-sta-ifc.h"

namespace wlan::simulation {

class StationIfc;

class SimFrame {
 public:
  enum SimFrameType { FRAME_TYPE_MGMT, FRAME_TYPE_CTRL, FRAME_TYPE_DATA };

  explicit SimFrame(StationIfc* sender) : sender_(sender){};
  virtual ~SimFrame();

  // Frame type identifier
  virtual SimFrameType FrameType() const = 0;

  StationIfc* sender_;
};

class SimManagementFrame : public SimFrame {
 public:
  enum SimMgmtFrameType {
    FRAME_TYPE_BEACON,
    FRAME_TYPE_PROBE_REQ,
    FRAME_TYPE_PROBE_RESP,
    FRAME_TYPE_ASSOC_REQ,
    FRAME_TYPE_ASSOC_RESP,
    FRAME_TYPE_DISASSOC_REQ
  };

  explicit SimManagementFrame(StationIfc* sender) : SimFrame(sender){};

  ~SimManagementFrame() override;

  // Frame type identifier
  SimFrameType FrameType() const override;
  // Frame subtype identifier for management frames
  virtual SimMgmtFrameType MgmtFrameType() const = 0;
};

class SimBeaconFrame : public SimManagementFrame {
 public:
  explicit SimBeaconFrame(StationIfc* sender, const wlan_ssid_t& ssid, const common::MacAddr& bssid)
      : SimManagementFrame(sender), ssid_(ssid), bssid_(bssid){};

  ~SimBeaconFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  wlan_ssid_t ssid_;
  common::MacAddr bssid_;
};

class SimProbeReqFrame : public SimManagementFrame {
 public:
  explicit SimProbeReqFrame(StationIfc* sender, const common::MacAddr& src)
      : SimManagementFrame(sender), src_addr_(src){};

  ~SimProbeReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
};

class SimProbeRespFrame : public SimManagementFrame {
 public:
  explicit SimProbeRespFrame(StationIfc* sender, const common::MacAddr& src,
                             const common::MacAddr& dst, const wlan_ssid_t& ssid)
      : SimManagementFrame(sender), src_addr_(src), dst_addr_(dst), ssid_(ssid){};

  ~SimProbeRespFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr dst_addr_;
  wlan_ssid_t ssid_;
};

class SimAssocReqFrame : public SimManagementFrame {
 public:
  explicit SimAssocReqFrame(StationIfc* sender, const common::MacAddr& src,
                            const common::MacAddr bssid)
      : SimManagementFrame(sender), src_addr_(src), bssid_(bssid){};

  ~SimAssocReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr bssid_;
};

class SimAssocRespFrame : public SimManagementFrame {
 public:
  explicit SimAssocRespFrame(StationIfc* sender, const common::MacAddr& src,
                             const common::MacAddr& dst, const uint16_t status)
      : SimManagementFrame(sender), src_addr_(src), dst_addr_(dst), status_(status){};

  ~SimAssocRespFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr dst_addr_;
  uint16_t status_;
};

class SimDisassocReqFrame : public SimManagementFrame {
 public:
  explicit SimDisassocReqFrame(StationIfc* sender, const common::MacAddr& src,
                               const common::MacAddr& dst, const uint16_t reason)
      : SimManagementFrame(sender), src_addr_(src), dst_addr_(dst), reason_(reason){};

  ~SimDisassocReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr dst_addr_;
  uint16_t reason_;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_
