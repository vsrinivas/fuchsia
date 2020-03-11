// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_

#include <zircon/types.h>

#include <list>

#include "sim-sta-ifc.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/mac_frame.h"

namespace wlan::simulation {

class StationIfc;

typedef struct WlanRxInfo {
  wlan_channel_t channel;
  double signal_strength;
} WlanRxInfo;

typedef struct WlanTxInfo {
  wlan_channel_t channel;
} WlanTxInfo;

class InformationElement {
 public:
  enum SimIEType { IE_TYPE_CSA = 37, IE_TYPE_WPA1 = 221, IE_TYPE_WPA2 = 48 };

  explicit InformationElement() = default;
  virtual ~InformationElement();

  virtual SimIEType IEType() const = 0;
};

// IEEE Std 802.11-2016, 9.4.2.19
class CSAInformationElement : public InformationElement {
 public:
  explicit CSAInformationElement(bool switch_mode, uint8_t new_channel, uint8_t switch_count) {
    channel_switch_mode_ = switch_mode;
    new_channel_number_ = new_channel;
    channel_switch_count_ = switch_count;
  };

  ~CSAInformationElement() override;

  SimIEType IEType() const override;

  bool channel_switch_mode_;
  uint8_t new_channel_number_;
  uint8_t channel_switch_count_;
};

class SimFrame {
 public:
  enum SimFrameType { FRAME_TYPE_MGMT, FRAME_TYPE_CTRL, FRAME_TYPE_DATA };

  SimFrame() = default;
  virtual ~SimFrame();

  // Frame type identifier
  virtual SimFrameType FrameType() const = 0;
};

class SimManagementFrame : public SimFrame {
 public:
  enum SimMgmtFrameType {
    FRAME_TYPE_BEACON,
    FRAME_TYPE_PROBE_REQ,
    FRAME_TYPE_PROBE_RESP,
    FRAME_TYPE_ASSOC_REQ,
    FRAME_TYPE_ASSOC_RESP,
    FRAME_TYPE_DISASSOC_REQ,
    FRAME_TYPE_AUTH
  };

  SimManagementFrame(){};

  ~SimManagementFrame() override;

  // Frame type identifier
  SimFrameType FrameType() const override;
  // Frame subtype identifier for management frames
  virtual SimMgmtFrameType MgmtFrameType() const = 0;
  void AddCSAIE(const wlan_channel_t& channel, uint8_t channel_switch_count);
  std::shared_ptr<InformationElement> FindIE(InformationElement::SimIEType ie_type) const;
  void RemoveIE(InformationElement::SimIEType);

  std::list<std::shared_ptr<InformationElement>> IEs_;

 private:
  void AddIE(InformationElement::SimIEType ie_type, std::shared_ptr<InformationElement> ie);
};

class SimBeaconFrame : public SimManagementFrame {
 public:
  SimBeaconFrame() = default;
  explicit SimBeaconFrame(const wlan_ssid_t& ssid, const common::MacAddr& bssid)
      : ssid_(ssid), bssid_(bssid){};

  ~SimBeaconFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  wlan_ssid_t ssid_;
  common::MacAddr bssid_;
  wlan::CapabilityInfo capability_info_;
};

class SimProbeReqFrame : public SimManagementFrame {
 public:
  SimProbeReqFrame() = default;
  explicit SimProbeReqFrame(const common::MacAddr& src) : src_addr_(src){};

  ~SimProbeReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
};

class SimProbeRespFrame : public SimManagementFrame {
 public:
  SimProbeRespFrame() = default;
  explicit SimProbeRespFrame(const common::MacAddr& src, const common::MacAddr& dst,
                             const wlan_ssid_t& ssid)
      : src_addr_(src), dst_addr_(dst), ssid_(ssid){};

  ~SimProbeRespFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr dst_addr_;
  wlan_ssid_t ssid_;
  wlan::CapabilityInfo capability_info_;
};

class SimAssocReqFrame : public SimManagementFrame {
 public:
  SimAssocReqFrame() = default;
  explicit SimAssocReqFrame(const common::MacAddr& src, const common::MacAddr bssid)
      : src_addr_(src), bssid_(bssid){};

  ~SimAssocReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr bssid_;
};

class SimAssocRespFrame : public SimManagementFrame {
 public:
  SimAssocRespFrame() = default;
  explicit SimAssocRespFrame(const common::MacAddr& src, const common::MacAddr& dst,
                             const uint16_t status)
      : src_addr_(src), dst_addr_(dst), status_(status){};

  ~SimAssocRespFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr dst_addr_;
  uint16_t status_;
};

class SimDisassocReqFrame : public SimManagementFrame {
 public:
  SimDisassocReqFrame() = default;
  explicit SimDisassocReqFrame(const common::MacAddr& src, const common::MacAddr& dst,
                               const uint16_t reason)
      : src_addr_(src), dst_addr_(dst), reason_(reason){};

  ~SimDisassocReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr dst_addr_;
  uint16_t reason_;
};

// AUTH_TYPE used by AP and authentication frame
enum SimAuthType { AUTH_TYPE_OPEN, AUTH_TYPE_SHARED_KEY };

// Only one type of authentication frame for request and response
class SimAuthFrame : public SimManagementFrame {
 public:
  SimAuthFrame() = default;
  explicit SimAuthFrame(const common::MacAddr& src, const common::MacAddr& dst, uint16_t seq,
                        SimAuthType auth_type, uint16_t status)
      : src_addr_(src), dst_addr_(dst), seq_num_(seq), auth_type_(auth_type), status_(status){};

  ~SimAuthFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  common::MacAddr src_addr_;
  common::MacAddr dst_addr_;
  uint16_t seq_num_;
  SimAuthType auth_type_;
  uint16_t status_;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_
