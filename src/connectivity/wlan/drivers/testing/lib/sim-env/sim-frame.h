// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_

#include <zircon/types.h>

#include <list>
#include <memory>
#include <optional>

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

// Representative for security protocol, we don't have this field in real situation.
enum SimSecProtoType {
  SEC_PROTO_TYPE_OPEN,
  SEC_PROTO_TYPE_WEP,
  SEC_PROTO_TYPE_WPA1,
  SEC_PROTO_TYPE_WPA2
};

// AUTH_TYPE used by AP and authentication frame
enum SimAuthType { AUTH_TYPE_OPEN, AUTH_TYPE_SHARED_KEY };

class InformationElement {
 public:
  enum SimIEType { IE_TYPE_SSID = 0, IE_TYPE_CSA = 37, IE_TYPE_WPA1 = 221, IE_TYPE_WPA2 = 48 };

  explicit InformationElement() = default;
  virtual ~InformationElement();

  virtual SimIEType IEType() const = 0;

  // Return the IE as a buffer of bytes, in the 802.11-specified format for this IE type.
  virtual std::vector<uint8_t> ToRawIe() const = 0;
};

// IEEE Std 802.11-2016, 9.4.2.2
class SSIDInformationElement : public InformationElement {
 public:
  explicit SSIDInformationElement(const wlan_ssid_t& ssid) : ssid_(ssid){};

  SSIDInformationElement(const SSIDInformationElement& ssid_ie);

  ~SSIDInformationElement() override;

  SimIEType IEType() const override;

  std::vector<uint8_t> ToRawIe() const override;

  wlan_ssid_t ssid_;
};

// IEEE Std 802.11-2016, 9.4.2.19
class CSAInformationElement : public InformationElement {
 public:
  explicit CSAInformationElement(bool switch_mode, uint8_t new_channel, uint8_t switch_count) {
    channel_switch_mode_ = switch_mode;
    new_channel_number_ = new_channel;
    channel_switch_count_ = switch_count;
  };

  CSAInformationElement(const CSAInformationElement& csa_ie);

  ~CSAInformationElement() override;

  SimIEType IEType() const override;

  std::vector<uint8_t> ToRawIe() const override;

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

  virtual SimFrame* CopyFrame() const = 0;
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
    FRAME_TYPE_AUTH,
    FRAME_TYPE_DEAUTH,
  };

  SimManagementFrame(){};
  SimManagementFrame(const common::MacAddr& src, const common::MacAddr& dst)
      : src_addr_(src), dst_addr_(dst){};

  SimManagementFrame(const SimManagementFrame& mgmt_frame);
  ~SimManagementFrame() override;

  // Frame type identifier
  SimFrameType FrameType() const override;
  // Frame subtype identifier for management frames
  virtual SimMgmtFrameType MgmtFrameType() const = 0;
  void AddSSIDIE(const wlan_ssid_t& ssid);
  void AddCSAIE(const wlan_channel_t& channel, uint8_t channel_switch_count);
  void AddRawIes(fbl::Span<const uint8_t> raw_ies);
  std::shared_ptr<InformationElement> FindIE(InformationElement::SimIEType ie_type) const;
  void RemoveIE(InformationElement::SimIEType);

  common::MacAddr src_addr_ = {};
  common::MacAddr dst_addr_ = {};
  std::list<std::shared_ptr<InformationElement>> IEs_;
  std::vector<uint8_t> raw_ies_;
  // This is a brief alternative for security related IEs since we don't include entire IE for
  // security protocol such as WPA IE or RSNE IE.
  enum SimSecProtoType sec_proto_type_ = SEC_PROTO_TYPE_OPEN;

 private:
  void AddIE(InformationElement::SimIEType ie_type, std::shared_ptr<InformationElement> ie);
};

class SimBeaconFrame : public SimManagementFrame {
 public:
  SimBeaconFrame() = default;
  explicit SimBeaconFrame(const wlan_ssid_t& ssid, const common::MacAddr& bssid);

  SimBeaconFrame(const SimBeaconFrame& beacon);

  ~SimBeaconFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  SimFrame* CopyFrame() const override;

  common::MacAddr bssid_;
  zx::duration interval_;
  wlan::CapabilityInfo capability_info_;
};

class SimProbeReqFrame : public SimManagementFrame {
 public:
  SimProbeReqFrame() = default;
  explicit SimProbeReqFrame(const common::MacAddr& src) : SimManagementFrame(src, {}){};

  SimProbeReqFrame(const SimProbeReqFrame& probe_req);

  ~SimProbeReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  SimFrame* CopyFrame() const override;
};

class SimProbeRespFrame : public SimManagementFrame {
 public:
  SimProbeRespFrame() = default;
  explicit SimProbeRespFrame(const common::MacAddr& src, const common::MacAddr& dst,
                             const wlan_ssid_t& ssid);

  SimProbeRespFrame(const SimProbeRespFrame& probe_resp);

  ~SimProbeRespFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  SimFrame* CopyFrame() const override;

  wlan::CapabilityInfo capability_info_;
};

class SimAssocReqFrame : public SimManagementFrame {
 public:
  SimAssocReqFrame() = default;
  explicit SimAssocReqFrame(const common::MacAddr& src, const common::MacAddr bssid,
                            const wlan_ssid_t ssid)
      : SimManagementFrame(src, {}), bssid_(bssid), ssid_(ssid){};

  SimAssocReqFrame(const SimAssocReqFrame& assoc_req);

  ~SimAssocReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  SimFrame* CopyFrame() const override;

  common::MacAddr bssid_;
  wlan_ssid_t ssid_;
};

class SimAssocRespFrame : public SimManagementFrame {
 public:
  SimAssocRespFrame() = default;
  explicit SimAssocRespFrame(const common::MacAddr& src, const common::MacAddr& dst,
                             const uint16_t status)
      : SimManagementFrame(src, dst), status_(status){};

  SimAssocRespFrame(const SimAssocRespFrame& assoc_resp);

  ~SimAssocRespFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  SimFrame* CopyFrame() const override;

  uint16_t status_;
  wlan::CapabilityInfo capability_info_;
};

class SimDisassocReqFrame : public SimManagementFrame {
 public:
  SimDisassocReqFrame() = default;
  explicit SimDisassocReqFrame(const common::MacAddr& src, const common::MacAddr& dst,
                               const uint16_t reason)
      : SimManagementFrame(src, dst), reason_(reason){};

  SimDisassocReqFrame(const SimDisassocReqFrame& disassoc_req);

  ~SimDisassocReqFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  SimFrame* CopyFrame() const override;

  uint16_t reason_;
};

// Only one type of authentication frame for request and response
class SimAuthFrame : public SimManagementFrame {
 public:
  SimAuthFrame() = default;
  explicit SimAuthFrame(const common::MacAddr& src, const common::MacAddr& dst, uint16_t seq,
                        SimAuthType auth_type, uint16_t status)
      : SimManagementFrame(src, dst), seq_num_(seq), auth_type_(auth_type), status_(status){};

  SimAuthFrame(const SimAuthFrame& auth);

  ~SimAuthFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  SimFrame* CopyFrame() const override;

  uint16_t seq_num_;
  SimAuthType auth_type_;
  uint16_t status_;
};

class SimDeauthFrame : public SimManagementFrame {
 public:
  SimDeauthFrame() = default;
  explicit SimDeauthFrame(const common::MacAddr& src, const common::MacAddr& dst, uint16_t reason)
      : SimManagementFrame(src, dst), reason_(reason){};

  SimDeauthFrame(const SimDeauthFrame& deauth);

  ~SimDeauthFrame() override;

  SimMgmtFrameType MgmtFrameType() const override;

  SimFrame* CopyFrame() const override;

  uint16_t reason_;
};

// No support for contention-free data frames, aggregation or fragmentation for now
// Assumes singular MSDU frames
// reassembly, decryption also not supported
class SimDataFrame : public SimFrame {
 public:
  enum SimDataFrameType { FRAME_TYPE_QOS_DATA };

  SimDataFrame() = default;
  explicit SimDataFrame(bool toDS, bool fromDS, common::MacAddr addr1, common::MacAddr addr2,
                        common::MacAddr addr3, std::optional<uint16_t> qosControl,
                        std::vector<uint8_t> payload)
      : toDS_(toDS),
        fromDS_(fromDS),
        addr1_(addr1),
        addr2_(addr2),
        addr3_(addr3),
        qosControl_(qosControl),
        payload_(payload) {}

  SimDataFrame(const SimDataFrame& data_frame);

  ~SimDataFrame() override;

  SimFrameType FrameType() const override;

  // Frame subtype identifier for data frames
  virtual SimDataFrameType DataFrameType() const = 0;

  // Control bits
  bool toDS_;
  bool fromDS_;

  // IEEE Std. 802.11-2016, 9.3.2.1 Table 9-26
  common::MacAddr addr1_;

  common::MacAddr addr2_;

  common::MacAddr addr3_;

  std::optional<common::MacAddr> addr4_;

  std::optional<uint16_t> qosControl_;

  // MAC Payload
  std::vector<uint8_t> payload_;
};

class SimQosDataFrame : public SimDataFrame {
 public:
  SimQosDataFrame() = default;
  explicit SimQosDataFrame(bool toDS, bool fromDS, common::MacAddr addr1, common::MacAddr addr2,
                           common::MacAddr addr3, std::optional<uint16_t> qosControl,
                           std::vector<uint8_t> payload)
      : SimDataFrame(toDS, fromDS, addr1, addr2, addr3, qosControl, payload) {}

  SimQosDataFrame(const SimQosDataFrame& qos_data);

  ~SimQosDataFrame() override;

  SimDataFrameType DataFrameType() const override;

  SimFrame* CopyFrame() const override;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_FRAME_H_
