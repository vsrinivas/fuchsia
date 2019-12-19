// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_ELEMENT_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_ELEMENT_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ddk/hw/wlan/ieee80211.h>
#include <ddk/protocol/wlan/info.h>
#include <wlan/common/bitfield.h>
#include <wlan/common/element_id.h>
#include <wlan/common/logging.h>
#include <wlan/common/macaddr.h>

namespace wlan {

// IEEE Std 802.11-2016, 9.4.2.1
struct ElementHeader {
  uint8_t id;
  uint8_t len;
} __PACKED;

constexpr size_t kMaxSsidLen = 32;

// IEEE 802.11-2016 9.4.2.3.
// The MSB in a rate indicates "basic rate" and is ignored during comparison.
// Rates are in 0.5Mbps increment: 12 -> 6 Mbps, 11 -> 5.5 Mbps, etc.
struct SupportedRate : public common::BitField<uint8_t> {
  constexpr SupportedRate() = default;
  constexpr explicit SupportedRate(uint8_t val) : common::BitField<uint8_t>(val) {}
  constexpr explicit SupportedRate(uint8_t val, bool is_basic) : common::BitField<uint8_t>(val) {
    set_is_basic(static_cast<uint8_t>(is_basic));
  }

  static SupportedRate basic(uint8_t rate) { return SupportedRate(rate, true); }
  static SupportedRate raw(uint8_t rate) { return SupportedRate(rate); }

  WLAN_BIT_FIELD(rate, 0, 7)
  WLAN_BIT_FIELD(is_basic, 7, 1)

  operator uint8_t() const { return val(); }
  bool operator==(const SupportedRate& other) const { return rate() == other.rate(); }
  bool operator!=(const SupportedRate& other) const { return !this->operator==(other); }
  bool operator<(const SupportedRate& other) const { return rate() < other.rate(); }
  bool operator>(const SupportedRate& other) const { return rate() > other.rate(); }
} __PACKED;

static constexpr size_t kMaxSupportedRatesLen = 8;

// IEEE Std 802.11-2016, 9.4.2.4
struct DsssParamSet {
  uint8_t current_chan;
};

// IEEE Std 802.11-2016, 9.4.2.5
struct CfParamSet {
  uint8_t count;
  uint8_t period;
  uint16_t max_duration;
  uint16_t dur_remaining;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.6
class BitmapControl : public common::BitField<uint8_t> {
 public:
  BitmapControl() = default;
  explicit BitmapControl(uint8_t raw) : BitField(raw) {}
  WLAN_BIT_FIELD(group_traffic_ind, 0, 1)
  WLAN_BIT_FIELD(offset, 1, 7)
};

// IEEE Std 802.11-2016, 9.4.2.6
struct TimHeader {
  uint8_t dtim_count;
  uint8_t dtim_period;
  BitmapControl bmp_ctrl;
} __PACKED;

constexpr size_t kMaxTimBitmapLen = 251;

// IEEE Std 802.11-2016, 9.4.2.9. Figure 9-131, 9-132.
struct SubbandTriplet {
  uint8_t first_channel_number;
  uint8_t number_of_channels;
  uint8_t max_tx_power;  // dBm
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.9
struct Country {
  static constexpr size_t kCountryLen = 3;
  uint8_t data[kCountryLen];
} __PACKED;
static_assert(sizeof(Country) == Country::kCountryLen);

const uint16_t kEapolProtocolId = 0x888E;

// IEEE Std 802.11-2016, 9.4.2.98
struct MeshConfiguration {
  enum PathSelProtoId : uint8_t {
    kHwmp = 1u,
  };

  enum PathSelMetricId : uint8_t {
    kAirtime = 1u,
  };

  enum CongestCtrlModeId : uint8_t {
    kCongestCtrlInactive = 0u,
    kCongestCtrlSignaling = 1u,
  };

  enum SyncMethodId : uint8_t {
    kNeighborOffsetSync = 1u,
  };

  enum AuthProtoId : uint8_t {
    kNoAuth = 0u,
    kSae = 1u,
    kIeee8021X = 2u,
  };

  struct MeshFormationInfo : public common::BitField<uint8_t> {
    MeshFormationInfo() = default;
    explicit MeshFormationInfo(uint8_t raw) : BitField(raw) {}

    WLAN_BIT_FIELD(connected_to_mesh_gate, 0, 1)
    WLAN_BIT_FIELD(num_peerings, 1, 6)
    WLAN_BIT_FIELD(connected_to_as, 7, 1)
  } __PACKED;

  ::fuchsia::wlan::mlme::MeshConfiguration ToFidl() const {
    ::fuchsia::wlan::mlme::MeshConfiguration ret;
    ret.active_path_sel_proto_id = static_cast<uint8_t>(active_path_sel_proto_id);
    ret.active_path_sel_metric_id = static_cast<uint8_t>(active_path_sel_metric_id);
    ret.congest_ctrl_method_id = static_cast<uint8_t>(congest_ctrl_method_id);
    ret.sync_method_id = static_cast<uint8_t>(sync_method_id);
    ret.auth_proto_id = static_cast<uint8_t>(auth_proto_id);
    ret.mesh_formation_info = mesh_formation_info.val();
    ret.mesh_capability = mesh_capability.val();
    return ret;
  }

  static MeshConfiguration FromFidl(const ::fuchsia::wlan::mlme::MeshConfiguration& f) {
    return MeshConfiguration{
        .active_path_sel_proto_id = static_cast<PathSelProtoId>(f.active_path_sel_proto_id),
        .active_path_sel_metric_id = static_cast<PathSelMetricId>(f.active_path_sel_metric_id),
        .congest_ctrl_method_id = static_cast<CongestCtrlModeId>(f.congest_ctrl_method_id),
        .sync_method_id = static_cast<SyncMethodId>(f.sync_method_id),
        .auth_proto_id = static_cast<AuthProtoId>(f.auth_proto_id),
        .mesh_formation_info = MeshFormationInfo(f.mesh_formation_info),
        .mesh_capability = MeshCapability(f.mesh_capability),
    };
  }

  struct MeshCapability : public common::BitField<uint8_t> {
    MeshCapability() = default;
    explicit MeshCapability(uint8_t raw) : BitField(raw) {}

    WLAN_BIT_FIELD(accepting_additional_peerings, 0, 1)
    WLAN_BIT_FIELD(mcca_supported, 1, 1)
    WLAN_BIT_FIELD(mcca_enabled, 2, 1)
    WLAN_BIT_FIELD(forwarding, 3, 1)
    WLAN_BIT_FIELD(mbca_enabled, 4, 1)
    WLAN_BIT_FIELD(tbtt_adjusting, 5, 1)
    WLAN_BIT_FIELD(power_save_level, 6, 1)
    // bit 7 is reserved
  } __PACKED;

  PathSelProtoId active_path_sel_proto_id;
  PathSelMetricId active_path_sel_metric_id;
  CongestCtrlModeId congest_ctrl_method_id;
  SyncMethodId sync_method_id;
  AuthProtoId auth_proto_id;
  MeshFormationInfo mesh_formation_info;
  MeshCapability mesh_capability;
} __PACKED;

constexpr size_t kMaxMeshIdLen = 32;

// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-478
struct PreqFlags : public common::BitField<uint8_t> {
  WLAN_BIT_FIELD(gate_announcement, 0, 1)
  WLAN_BIT_FIELD(addressing_mode, 1, 1)
  WLAN_BIT_FIELD(proactive, 2, 1)
  // bits 3-5 reserved
  WLAN_BIT_FIELD(addr_ext, 6, 1)
  // bit 7 reserved
};

// Fixed-length fields of the PREQ element that precede
// the optional Originator External Address field.
// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-477
struct PreqHeader {
  PreqFlags flags;
  uint8_t hop_count;
  uint8_t element_ttl;
  uint32_t path_discovery_id;
  common::MacAddr originator_addr;
  uint32_t originator_hwmp_seqno;
} __PACKED;

static_assert(sizeof(PreqHeader) == 17);

// Fixed-length fields of the PREQ elements that follow the optional Originator
// External Address field and precede the variable length per-target fields.
// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-477
struct PreqMiddle {
  uint32_t lifetime;
  uint32_t metric;
  uint8_t target_count;
} __PACKED;

static_assert(sizeof(PreqMiddle) == 9);

// IEEE Std 802.11-2016, 9.4.2.113
constexpr size_t kPreqMaxTargets = 20;

// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-479
struct PreqPerTargetFlags : public common::BitField<uint8_t> {
  WLAN_BIT_FIELD(target_only, 0, 1)
  // bit 1 reserved
  WLAN_BIT_FIELD(usn, 2, 1)
  // bits 3-7 reserved
};

// An entry of the variable-length part of PREQ
// IEEE Std 802.11-2016, 9.4.2.113, Figure 9-477
struct PreqPerTarget {
  PreqPerTargetFlags flags;
  common::MacAddr target_addr;
  uint32_t target_hwmp_seqno;
} __PACKED;

static_assert(sizeof(PreqPerTarget) == 11);

// IEEE Std 802.11-2016, 9.4.2.114, Figure 9-481
struct PrepFlags : public common::BitField<uint8_t> {
  PrepFlags() = default;
  explicit PrepFlags(uint8_t raw_value) : common::BitField<uint8_t>(raw_value) {}

  // bits 0-5 reserved
  WLAN_BIT_FIELD(addr_ext, 6, 1)
  // bit 7 reserved
};

// Fixed-length fields of the PREP element that precede
// the optional Target External Address field.
// IEEE Std 802.11-2016, 9.4.2.114, Figure 9-480
struct PrepHeader {
  PrepFlags flags;
  uint8_t hop_count;
  uint8_t element_ttl;
  common::MacAddr target_addr;
  uint32_t target_hwmp_seqno;
} __PACKED;

static_assert(sizeof(PrepHeader) == 13);

// Fixed-length fields of the PREP element that follow
// the optional Target External Address field.
// IEEE Std 802.11-2016, 9.4.2.114, Figure 9-480
struct PrepTail {
  uint32_t lifetime;
  uint32_t metric;
  common::MacAddr originator_addr;
  uint32_t originator_hwmp_seqno;
} __PACKED;

static_assert(sizeof(PrepTail) == 18);

// Fixed-length fields of the PERR element that precede the variable-length
// per-destination fields.
// IEEE Std 802.11-2016, 9.4.2.115
struct PerrHeader {
  uint8_t element_ttl;
  uint8_t num_destinations;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.115, Figure 9-483
struct PerrPerDestinationFlags : public common::BitField<uint8_t> {
  // bits 0-5 reserved
  WLAN_BIT_FIELD(addr_ext, 6, 1)
  // bit 7 reserved
};

// Fixed-length fields of the per-destination chunk of the PERR element
// that precede the optional "Destination External Address" field.
// IEEE Std 802.11-2016, 9.4.2.115
struct PerrPerDestinationHeader {
  PerrPerDestinationFlags flags;
  common::MacAddr dest_addr;
  uint32_t hwmp_seqno;
} __PACKED;

// The fixed-length field of the per-destination chunk of the PERR element
// that follows the optional "Destination External Address" field.
// IEEE Std 802.11-2016, 9.4.2.115
struct PerrPerDestinationTail {
  uint16_t reason_code;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.115
constexpr size_t kPerrMaxDestinations = 19;

constexpr size_t kPerrMaxDestinationSize =
    sizeof(PerrPerDestinationHeader) + sizeof(common::MacAddr) + sizeof(PerrPerDestinationTail);

// IEEE Std 802.11-2016, 9.4.1.17
class QosInfo : public common::BitField<uint8_t> {
 public:
  constexpr explicit QosInfo(uint8_t value) : common::BitField<uint8_t>(value) {}
  constexpr QosInfo() = default;

  // AP specific QoS Info structure: IEEE Std 802.11-2016, 9.4.1.17, Figure 9-82
  WLAN_BIT_FIELD(edca_param_set_update_count, 0, 4)
  WLAN_BIT_FIELD(qack, 4, 1)
  WLAN_BIT_FIELD(queue_request, 5, 1)
  WLAN_BIT_FIELD(txop_request, 6, 1)
  // 8th bit reserved

  // Non-AP STA specific QoS Info structure: IEEE Std 802.11-2016, 9.4.1.17,
  // Figure 9-83
  WLAN_BIT_FIELD(ac_vo_uapsd_flag, 0, 1)
  WLAN_BIT_FIELD(ac_vi_uapsd_flag, 1, 1)
  WLAN_BIT_FIELD(ac_bk_uapsd_flag, 2, 1)
  WLAN_BIT_FIELD(ac_be_uapsd_flag, 3, 1)
  // qack already defined in AP specific structure.
  WLAN_BIT_FIELD(max_sp_len, 5, 1)
  WLAN_BIT_FIELD(more_data_ack, 6, 1)
  // 8th bit reserved
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30, Table 9-139
enum TsDirection : uint8_t {
  kUplink = 0,
  kDownlink = 1,
  kDirectLink = 2,
  kBidirectionalLink = 3,
};

// IEEE Std 802.11-2016, 9.4.2.30, Table 9-140
enum TsAccessPolicy : uint8_t {
  // 0 reserved
  kEdca = 1,
  kHccaSpca = 2,
  kMixedMode = 3,
};

// IEEE Std 802.11-2016, 9.4.2.30, Table 9-141
namespace ts_ack_policy {
enum TsAckPolicy : uint8_t {
  kNormalAck = 0,
  kNoAck = 1,
  // 2 reserved
  kBlockAck = 3,
};
}  // namespace ts_ack_policy

// IEEE Std 802.11-2016, 9.4.2.30, Table 9-142
// Only used if TsInfo's access policy uses EDCA.
// Schedule Setting depends on TsInfo's ASPD and schedule fields.
enum TsScheduleSetting : uint8_t {
  kNoSchedule = 0,
  kUnschedledApsd = 1,
  kScheduledPsmp_GcrSp = 2,
  kScheduledApsd = 3,
};

// IEEE Std 802.11-2016, 9.4.2.30, Figure 9-266
class TsInfoPart1 : public common::BitField<uint16_t> {
 public:
  constexpr explicit TsInfoPart1(uint16_t params) : common::BitField<uint16_t>(params) {}
  constexpr TsInfoPart1() = default;

  WLAN_BIT_FIELD(traffic_type, 0, 1)
  WLAN_BIT_FIELD(tsid, 1, 4)
  WLAN_BIT_FIELD(direction, 5, 2)
  WLAN_BIT_FIELD(access_policy, 7, 2)
  WLAN_BIT_FIELD(aggregation, 9, 1)
  WLAN_BIT_FIELD(apsd, 10, 1)
  WLAN_BIT_FIELD(user_priority, 11, 3)
  WLAN_BIT_FIELD(ack_policy, 14, 2)
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30, Figure 9-266
class TsInfoPart2 : public common::BitField<uint8_t> {
 public:
  constexpr explicit TsInfoPart2(uint8_t params) : common::BitField<uint8_t>(params) {}
  constexpr TsInfoPart2() = default;

  WLAN_BIT_FIELD(schedule, 0, 1)
  // Bit 17-23 reserved.
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30, Figure 9-266
// Note: In order to use a 3 byte packed struct, the TsInfo was split into two
// parts.
struct TsInfo {
  TsInfoPart1 p1;
  TsInfoPart2 p2;

  bool IsValidAggregation() const {
    if (p1.access_policy() == TsAccessPolicy::kHccaSpca) {
      return true;
    }
    return p1.access_policy() == TsAccessPolicy::kEdca && p2.schedule();
  }

  bool IsScheduleReserved() const { return p1.access_policy() != TsAccessPolicy::kEdca; }

  TsScheduleSetting GetScheduleSetting() const {
    return static_cast<TsScheduleSetting>(p1.apsd() | (p2.schedule() << 1));
  }
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30, Figure 9-267
struct NominalMsduSize : public common::BitField<uint16_t> {
  constexpr explicit NominalMsduSize(uint16_t params) : common::BitField<uint16_t>(params) {}
  constexpr NominalMsduSize() = default;

  WLAN_BIT_FIELD(size, 0, 15) WLAN_BIT_FIELD(fixed, 15, 1)
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30
struct Tspec {
  // TODO(hahnr): The element will for now only be read by the AP when received
  // from an associated client and there is no need for providing a custom
  // constructor yet.

  TsInfo ts_info;
  NominalMsduSize nominal_msdu_size;
  uint16_t max_msdu_size;
  uint32_t min_service_interval;
  uint32_t max_service_interval;
  uint32_t inactivity_interval;
  uint32_t suspension_interval;
  uint32_t service_start_time;
  uint32_t min_data_rate;
  uint32_t mean_data_rate;
  uint32_t peak_data_rate;
  uint32_t burst_size;
  uint32_t delay_bound;
  uint32_t min_phy_rate;
  uint16_t surplus_bw_allowance;
  uint16_t medium_time;

  // TODO(hahnr): Add min/mean/peak data rate support based on the provided
  // fields.
  // TODO(hahnr): Add min PHY rate support based on the provided field.
  // TODO(hahnr): Add DMG support.
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.2
// Note this is a field of HtCapabilities element.
class HtCapabilityInfo : public common::BitField<uint16_t> {
 public:
  constexpr explicit HtCapabilityInfo(uint16_t ht_cap_info)
      : common::BitField<uint16_t>(ht_cap_info) {}
  constexpr HtCapabilityInfo() = default;

  WLAN_BIT_FIELD(ldpc_coding_cap, 0, 1)
  WLAN_BIT_FIELD(chan_width_set, 1, 1)  // In spec: Supported Channel Width Set
  WLAN_BIT_FIELD(sm_power_save, 2, 2)   // Spatial Multiplexing Power Save
  WLAN_BIT_FIELD(greenfield, 4, 1)      // HT-Greenfield.
  WLAN_BIT_FIELD(short_gi_20, 5, 1)     // Short Guard Interval for 20 MHz
  WLAN_BIT_FIELD(short_gi_40, 6, 1)     // Short Guard Interval for 40 MHz
  WLAN_BIT_FIELD(tx_stbc, 7, 1)

  WLAN_BIT_FIELD(rx_stbc, 8, 2)             // maximum number of spatial streams. Up to 3.
  WLAN_BIT_FIELD(delayed_block_ack, 10, 1)  // HT-delayed Block Ack
  WLAN_BIT_FIELD(max_amsdu_len, 11, 1)
  WLAN_BIT_FIELD(dsss_in_40, 12, 1)  // DSSS/CCK Mode in 40 MHz
  WLAN_BIT_FIELD(reserved, 13, 1)
  WLAN_BIT_FIELD(intolerant_40, 14, 1)  // 40 MHz Intolerant
  WLAN_BIT_FIELD(lsig_txop_protect, 15, 1)

  enum ChanWidthSet {
    TWENTY_ONLY = 0,
    TWENTY_FORTY = 1,
  };

  enum SmPowerSave {
    STATIC = 0,
    DYNAMIC = 1,
    RESERVED = 2,
    DISABLED = 3,
  };

  enum MaxAmsduLen {
    OCTETS_3839 = 0,
    OCTETS_7935 = 1,
  };
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.3
class AmpduParams : public common::BitField<uint8_t> {
 public:
  constexpr explicit AmpduParams(uint8_t params) : common::BitField<uint8_t>(params) {}
  constexpr AmpduParams() = default;

  WLAN_BIT_FIELD(exponent, 0, 2)           // Maximum A-MPDU Length Exponent.
  WLAN_BIT_FIELD(min_start_spacing, 2, 3)  // Minimum MPDU Start Spacing.
  WLAN_BIT_FIELD(reserved, 5, 3)

  size_t max_ampdu_len() const { return (1 << (13 + exponent())) - 1; }

  enum MinMPDUStartSpacing {
    NO_RESTRICT = 0,
    QUARTER_USEC = 1,
    HALF_USEC = 2,
    ONE_USEC = 3,
    TWO_USEC = 4,
    FOUR_USEC = 5,
    EIGHT_USEC = 6,
    SIXTEEN_USEC = 7,
  };
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsRxMcsHead : public common::BitField<uint64_t> {
 public:
  constexpr explicit SupportedMcsRxMcsHead(uint64_t val) : common::BitField<uint64_t>(val) {}
  constexpr SupportedMcsRxMcsHead() = default;

  bool Support(uint8_t mcs_index) const { return 1 == (1 & (bitmask() >> mcs_index)); }

  // HT-MCS table in IEEE Std 802.11-2016, Annex B.4.17.2
  // VHT-MCS tables in IEEE Std 802.11-2016, 21.5
  WLAN_BIT_FIELD(bitmask, 0, 64)
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsRxMcsTail : public common::BitField<uint32_t> {
 public:
  constexpr explicit SupportedMcsRxMcsTail(uint32_t val) : common::BitField<uint32_t>(val) {}
  constexpr SupportedMcsRxMcsTail() = default;

  WLAN_BIT_FIELD(bitmask, 0, 13)
  WLAN_BIT_FIELD(reserved1, 13, 3)
  WLAN_BIT_FIELD(highest_rate, 16, 10)  // Mbps. Rx Highest Supported Rate.
  WLAN_BIT_FIELD(reserved2, 26, 6)
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsTxMcs : public common::BitField<uint32_t> {
 public:
  constexpr explicit SupportedMcsTxMcs(uint32_t chunk) : common::BitField<uint32_t>(chunk) {}
  constexpr SupportedMcsTxMcs() = default;

  WLAN_BIT_FIELD(set_defined, 0, 1)  // Add 96 for the original bit location
  WLAN_BIT_FIELD(rx_diff, 1, 1)
  WLAN_BIT_FIELD(max_ss, 2, 2)
  WLAN_BIT_FIELD(ueqm, 4, 1)  // Transmit Unequal Modulation.
  WLAN_BIT_FIELD(reserved, 5, 27)

  uint8_t max_ss_human() const { return max_ss() + 1; }
  void set_max_ss_human(uint8_t num) {
    constexpr uint8_t kLowerbound = 1;
    constexpr uint8_t kUpperbound = 4;
    if (num < kLowerbound)
      num = kLowerbound;
    if (num > kUpperbound)
      num = kUpperbound;
    set_max_ss(num - 1);
  }
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.4
struct SupportedMcsSet {
  SupportedMcsRxMcsHead rx_mcs_head;
  SupportedMcsRxMcsTail rx_mcs_tail;
  SupportedMcsTxMcs tx_mcs;

  // TODO(porce): Implement accessors
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.5
class HtExtCapabilities : public common::BitField<uint16_t> {
 public:
  constexpr explicit HtExtCapabilities(uint16_t ht_ext_cap)
      : common::BitField<uint16_t>(ht_ext_cap) {}
  constexpr HtExtCapabilities() = default;

  WLAN_BIT_FIELD(pco, 0, 1)
  WLAN_BIT_FIELD(pco_transition, 1, 2)
  WLAN_BIT_FIELD(reserved1, 3, 5)
  WLAN_BIT_FIELD(mcs_feedback, 8, 2)
  WLAN_BIT_FIELD(htc_ht_support, 10, 1)
  WLAN_BIT_FIELD(rd_responder, 11, 1)
  WLAN_BIT_FIELD(reserved2, 12, 4)

  enum PcoTransitionTime {
    PCO_RESERVED = 0,  // Often translated as "No transition".
    PCO_400_USEC = 1,
    PCO_1500_USEC = 2,
    PCO_5000_USEC = 3,
  };

  enum McsFeedback {
    MCS_NOFEEDBACK = 0,
    MCS_RESERVED = 1,
    MCS_UNSOLICIED = 2,
    MCS_BOTH = 3,
  };

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.6
class TxBfCapability : public common::BitField<uint32_t> {
 public:
  constexpr explicit TxBfCapability(uint32_t txbf_cap) : common::BitField<uint32_t>(txbf_cap) {}
  constexpr TxBfCapability() = default;

  WLAN_BIT_FIELD(implicit_rx, 0, 1)
  WLAN_BIT_FIELD(rx_stag_sounding, 1, 1)
  WLAN_BIT_FIELD(tx_stag_sounding, 2, 1)
  WLAN_BIT_FIELD(rx_ndp, 3, 1)
  WLAN_BIT_FIELD(tx_ndp, 4, 1)
  WLAN_BIT_FIELD(implicit, 5, 1)
  WLAN_BIT_FIELD(calibration, 6, 2)
  WLAN_BIT_FIELD(csi, 8, 1)  // Explicit CSI Transmit Beamforming.

  WLAN_BIT_FIELD(noncomp_steering, 9, 1)  // Explicit Noncompressed Steering
  WLAN_BIT_FIELD(comp_steering, 10, 1)    // Explicit Compressed Steering
  WLAN_BIT_FIELD(csi_feedback, 11, 2)
  WLAN_BIT_FIELD(noncomp_feedback, 13, 2)
  WLAN_BIT_FIELD(comp_feedback, 15, 2)
  WLAN_BIT_FIELD(min_grouping, 17, 2)
  WLAN_BIT_FIELD(csi_antennas, 19, 2)

  WLAN_BIT_FIELD(noncomp_steering_ants, 21, 2)
  WLAN_BIT_FIELD(comp_steering_ants, 23, 2)
  WLAN_BIT_FIELD(csi_rows, 25, 2)
  WLAN_BIT_FIELD(chan_estimation, 27, 2)
  WLAN_BIT_FIELD(reserved, 29, 3)

  enum Calibration {
    CALIBRATION_NONE = 0,
    CALIBRATION_RESPOND_NOINITIATE = 1,
    CALIBRATION_RESERVED = 2,
    CALIBRATION_RESPOND_INITIATE = 3,
  };

  enum Feedback {
    // Shared for csi_feedback, noncomp_feedback, comp_feedback
    FEEDBACK_NONE = 0,
    FEEDBACK_DELAYED = 1,
    FEEDBACK_IMMEDIATE = 2,
    FEEDBACK_DELAYED_IMMEDIATE = 3,
  };

  enum MinGroup {
    MIN_GROUP_ONE = 0,  // Meaning no grouping
    MIN_GROUP_ONE_TWO = 1,
    MIN_GROUP_ONE_FOUR = 2,
    MIN_GROUP_ONE_TWO_FOUR = 3,
  };

  uint8_t csi_antennas_human() const { return csi_antennas() + 1; }
  void set_csi_antennas_human(uint8_t num) {
    constexpr uint8_t kLowerbound = 1;
    constexpr uint8_t kUpperbound = 4;
    if (num < kLowerbound)
      num = kLowerbound;
    if (num > kUpperbound)
      num = kUpperbound;
    set_csi_antennas(num - 1);
  }

  uint8_t noncomp_steering_ants_human() const { return noncomp_steering_ants() + 1; }
  void set_noncomp_steering_ants_human(uint8_t num) {
    constexpr uint8_t kLowerbound = 1;
    constexpr uint8_t kUpperbound = 4;
    if (num < kLowerbound)
      num = kLowerbound;
    if (num > kUpperbound)
      num = kUpperbound;
    set_noncomp_steering_ants(num - 1);
  }

  uint8_t comp_steering_ants_human() const { return comp_steering_ants() + 1; }
  void set_comp_steering_ants_human(uint8_t num) {
    constexpr uint8_t kLowerbound = 1;
    constexpr uint8_t kUpperbound = 4;
    if (num < kLowerbound)
      num = kLowerbound;
    if (num > kUpperbound)
      num = kUpperbound;
    set_comp_steering_ants(num - 1);
  }

  uint8_t csi_rows_human() const { return csi_rows() + 1; }
  void set_csi_rows_human(uint8_t num) {
    constexpr uint8_t kLowerbound = 1;
    constexpr uint8_t kUpperbound = 4;
    if (num < kLowerbound)
      num = kLowerbound;
    if (num > kUpperbound)
      num = kUpperbound;
    set_csi_rows(num - 1);
  }

  uint8_t chan_estimation_human() const { return chan_estimation() + 1; }
  void set_chan_estimation_human(uint8_t num) {
    constexpr uint8_t kLowerbound = 1;
    constexpr uint8_t kUpperbound = 4;
    if (num < kLowerbound)
      num = kLowerbound;
    if (num > kUpperbound)
      num = kUpperbound;
    set_chan_estimation(num - 1);
  }

} __PACKED;

class AselCapability : public common::BitField<uint8_t> {
 public:
  constexpr explicit AselCapability(uint8_t asel_cap) : common::BitField<uint8_t>(asel_cap) {}
  constexpr AselCapability() = default;

  WLAN_BIT_FIELD(asel, 0, 1)
  WLAN_BIT_FIELD(csi_feedback_tx_asel, 1,
                 1)  // Explicit CSI Feedback based Transmit ASEL
  WLAN_BIT_FIELD(ant_idx_feedback_tx_asel, 2, 1)
  WLAN_BIT_FIELD(explicit_csi_feedback, 3, 1)
  WLAN_BIT_FIELD(antenna_idx_feedback, 4, 1)
  WLAN_BIT_FIELD(rx_asel, 5, 1)
  WLAN_BIT_FIELD(tx_sounding_ppdu, 6, 1)
  WLAN_BIT_FIELD(reserved, 7, 1)

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56
struct HtCapabilities {
  HtCapabilityInfo ht_cap_info;
  AmpduParams ampdu_params;
  SupportedMcsSet mcs_set;
  HtExtCapabilities ht_ext_cap;
  TxBfCapability txbf_cap;
  AselCapability asel_cap;

  static HtCapabilities FromDdk(const ieee80211_ht_capabilities_t& ddk) {
    HtCapabilities dst{};
    dst.ht_cap_info.set_val(ddk.ht_capability_info);
    dst.ampdu_params.set_val(ddk.ampdu_params);
    dst.mcs_set.rx_mcs_head.set_val(ddk.supported_mcs_set.fields.rx_mcs_head);
    dst.mcs_set.rx_mcs_tail.set_val(ddk.supported_mcs_set.fields.rx_mcs_tail);
    dst.mcs_set.tx_mcs.set_val(ddk.supported_mcs_set.fields.tx_mcs);
    dst.ht_ext_cap.set_val(ddk.ht_ext_capabilities);
    dst.txbf_cap.set_val(ddk.tx_beamforming_capabilities);
    dst.asel_cap.set_val(ddk.asel_capabilities);
    return dst;
  }

  ieee80211_ht_capabilities_t ToDdk() const {
    ieee80211_ht_capabilities_t ddk{};
    ddk.ht_capability_info = ht_cap_info.val();
    ddk.ampdu_params = ampdu_params.val();
    ddk.supported_mcs_set.fields.rx_mcs_head = mcs_set.rx_mcs_head.val();
    ddk.supported_mcs_set.fields.rx_mcs_tail = mcs_set.rx_mcs_tail.val();
    ddk.supported_mcs_set.fields.tx_mcs = mcs_set.tx_mcs.val();
    ddk.ht_ext_capabilities = ht_ext_cap.val();
    ddk.tx_beamforming_capabilities = txbf_cap.val();
    ddk.asel_capabilities = asel_cap.val();
    return ddk;
  }

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.57
// Note this is a field within HtOperation element.
class HtOpInfoHead : public common::BitField<uint32_t> {
 public:
  constexpr explicit HtOpInfoHead(uint32_t op_info) : common::BitField<uint32_t>(op_info) {}
  constexpr HtOpInfoHead() = default;

  WLAN_BIT_FIELD(secondary_chan_offset, 0, 2)
  WLAN_BIT_FIELD(sta_chan_width, 2, 1)
  WLAN_BIT_FIELD(rifs_mode, 3, 1)
  WLAN_BIT_FIELD(reserved1, 4,
                 4)  // Note 802.11n D1.10 implementaions use these.

  WLAN_BIT_FIELD(ht_protect, 8, 2)
  WLAN_BIT_FIELD(nongreenfield_present, 10,
                 1)  // Nongreenfield HT STAs present.

  WLAN_BIT_FIELD(reserved2, 11,
                 1)                   // Note 802.11n D1.10 implementations use these.
  WLAN_BIT_FIELD(obss_non_ht, 12, 1)  // OBSS Non-HT STAs present.
  // IEEE 802.11-2016 Figure 9-339 has an incosistency so this is Fuchsia
  // interpretation: The channel number for the second segment in a 80+80 Mhz
  // channel
  WLAN_BIT_FIELD(center_freq_seg2, 13, 8)  // VHT
  WLAN_BIT_FIELD(reserved3, 21, 3)

  WLAN_BIT_FIELD(reserved4, 24, 6)
  WLAN_BIT_FIELD(dual_beacon, 30, 1)
  WLAN_BIT_FIELD(dual_cts_protect, 31, 1)

  enum SecChanOffset {
    SECONDARY_NONE = 0,   // No secondary channel
    SECONDARY_ABOVE = 1,  // Secondary channel is above the primary channel
    RESERVED = 2,
    SECONDARY_BELOW = 3,  // Secondary channel is below the primary channel
  };

  enum StaChanWidth {
    TWENTY = 0,  // MHz
    ANY = 1,     // Any in the Supported Channel Width set
  };

  enum HtProtect {
    NONE = 0,
    NONMEMBER = 1,
    TWENTY_MHZ = 2,
    NON_HT_MIXED = 3,
  };
} __PACKED;

class HtOpInfoTail : public common::BitField<uint8_t> {
 public:
  constexpr explicit HtOpInfoTail(uint8_t val) : common::BitField<uint8_t>(val) {}
  constexpr HtOpInfoTail() = default;

  WLAN_BIT_FIELD(stbc_beacon, 0, 1)  // Add 32 for the original bit location.
  WLAN_BIT_FIELD(lsig_txop_protect, 1, 1)
  WLAN_BIT_FIELD(pco_active, 2, 1)
  WLAN_BIT_FIELD(pco_phase, 3, 1)
  WLAN_BIT_FIELD(reserved5, 4, 4)
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.57
struct HtOperation {
  uint8_t primary_chan;  // Primary 20 MHz channel.

  // Implementation hack to support 40bits bitmap.
  HtOpInfoHead head;
  HtOpInfoTail tail;
  SupportedMcsSet basic_mcs_set;

  static HtOperation FromDdk(const wlan_ht_op_t& ddk) {
    HtOperation dst{};
    dst.primary_chan = ddk.primary_chan;
    dst.head.set_val(ddk.head);
    dst.tail.set_val(ddk.tail);
    dst.basic_mcs_set.rx_mcs_head.set_val(ddk.rx_mcs_head);
    dst.basic_mcs_set.rx_mcs_tail.set_val(ddk.rx_mcs_tail);
    dst.basic_mcs_set.tx_mcs.set_val(ddk.tx_mcs);
    return dst;
  }

  wlan_ht_op_t ToDdk() const {
    wlan_ht_op_t ddk{};
    ddk.primary_chan = primary_chan;
    ddk.head = head.val();
    ddk.tail = tail.val();
    ddk.rx_mcs_head = basic_mcs_set.rx_mcs_head.val();
    ddk.rx_mcs_tail = basic_mcs_set.rx_mcs_tail.val();
    ddk.tx_mcs = basic_mcs_set.tx_mcs.val();
    return ddk;
  }

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.158.2
// Note this is a field of VhtCapabilities element
struct VhtCapabilitiesInfo : public common::BitField<uint32_t> {
 public:
  constexpr explicit VhtCapabilitiesInfo(uint32_t vht_cap_info)
      : common::BitField<uint32_t>(vht_cap_info) {}
  constexpr VhtCapabilitiesInfo() = default;

  WLAN_BIT_FIELD(max_mpdu_len, 0, 2)

  // Supported channel width set. See IEEE Std 802.11-2016, Table 9-250.
  WLAN_BIT_FIELD(supported_cbw_set, 2, 2)

  WLAN_BIT_FIELD(rx_ldpc, 4, 1)
  WLAN_BIT_FIELD(sgi_cbw80, 5, 1)   // CBW80 only
  WLAN_BIT_FIELD(sgi_cbw160, 6, 1)  // CBW160 and CBW80P80
  WLAN_BIT_FIELD(tx_stbc, 7, 1)
  WLAN_BIT_FIELD(rx_stbc, 8, 3)
  WLAN_BIT_FIELD(su_bfer, 11, 1)       // Single user beamformer capable
  WLAN_BIT_FIELD(su_bfee, 12, 1)       // Single user beamformee capable
  WLAN_BIT_FIELD(bfee_sts, 13, 3)      // Beamformee Space-time spreading
  WLAN_BIT_FIELD(num_sounding, 16, 3)  // number of sounding dimensions
  WLAN_BIT_FIELD(mu_bfer, 19, 1)       // Multi user beamformer capable
  WLAN_BIT_FIELD(mu_bfee, 20, 1)       // Multi user beamformee capable
  WLAN_BIT_FIELD(txop_ps, 21, 1)       // Txop power save mode
  WLAN_BIT_FIELD(htc_vht, 22, 1)
  WLAN_BIT_FIELD(max_ampdu_exp, 23, 3)
  WLAN_BIT_FIELD(link_adapt, 26, 2)  // VHT link adaptation capable
  WLAN_BIT_FIELD(rx_ant_pattern, 28, 1)
  WLAN_BIT_FIELD(tx_ant_pattern, 29, 1)

  // Extended number of spatial stream bandwidth supported
  // See IEEE Std 80.211-2016, Table 9-250.
  WLAN_BIT_FIELD(ext_nss_bw, 30, 2)

  enum MaxMpduLen {
    OCTETS_3895 = 0,
    OCTETS_7991 = 1,
    OCTETS_11454 = 2,
    // 3 reserved
  };

  enum VhtLinkAdaptation {
    LINK_ADAPT_NO_FEEDBACK = 0,
    // 1 reserved
    LINK_ADAPT_UNSOLICITED = 2,
    LINK_ADAPT_BOTH = 3,
  };

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.158.3
struct VhtMcsNss : public common::BitField<uint64_t> {
 public:
  constexpr explicit VhtMcsNss(uint64_t vht_mcs_nss) : common::BitField<uint64_t>(vht_mcs_nss) {}
  constexpr VhtMcsNss() = default;

  // Rx VHT-MCS Map
  WLAN_BIT_FIELD(rx_max_mcs_ss1, 0, 2)
  WLAN_BIT_FIELD(rx_max_mcs_ss2, 2, 2)
  WLAN_BIT_FIELD(rx_max_mcs_ss3, 4, 2)
  WLAN_BIT_FIELD(rx_max_mcs_ss4, 6, 2)
  WLAN_BIT_FIELD(rx_max_mcs_ss5, 8, 2)
  WLAN_BIT_FIELD(rx_max_mcs_ss6, 10, 2)
  WLAN_BIT_FIELD(rx_max_mcs_ss7, 12, 2)
  WLAN_BIT_FIELD(rx_max_mcs_ss8, 14, 2)

  WLAN_BIT_FIELD(rx_max_data_rate, 16, 13)
  WLAN_BIT_FIELD(max_nsts, 29, 3)

  // Tx VHT-MCS Map
  WLAN_BIT_FIELD(tx_max_mcs_ss1, 32, 2)
  WLAN_BIT_FIELD(tx_max_mcs_ss2, 34, 2)
  WLAN_BIT_FIELD(tx_max_mcs_ss3, 36, 2)
  WLAN_BIT_FIELD(tx_max_mcs_ss4, 38, 2)
  WLAN_BIT_FIELD(tx_max_mcs_ss5, 40, 2)
  WLAN_BIT_FIELD(tx_max_mcs_ss6, 42, 2)
  WLAN_BIT_FIELD(tx_max_mcs_ss7, 44, 2)
  WLAN_BIT_FIELD(tx_max_mcs_ss8, 46, 2)
  WLAN_BIT_FIELD(tx_max_data_rate, 48, 13)

  WLAN_BIT_FIELD(ext_nss_bw, 61, 1)
  // bit 62, 63 reserved

  enum VhtMcsSet {
    VHT_MCS_0_TO_7 = 0,
    VHT_MCS_0_TO_8 = 1,
    VHT_MCS_0_TO_9 = 2,
    VHT_MCS_NONE = 3,
  };

  uint8_t get_rx_max_mcs_ss(uint8_t ss_num) const {
    ZX_DEBUG_ASSERT(1 <= ss_num && ss_num <= 8);
    constexpr uint8_t kMcsBitOffset = 0;  // rx_max_mcs_ss1
    constexpr uint8_t kBitWidth = 2;
    uint8_t offset = kMcsBitOffset + (ss_num - 1) * kBitWidth;
    uint64_t mask = ((1ull << kBitWidth) - 1) << offset;
    return (val() & mask) >> offset;
  }

  uint8_t get_tx_max_mcs_ss(uint8_t ss_num) const {
    ZX_DEBUG_ASSERT(1 <= ss_num && ss_num <= 8);
    constexpr uint8_t kMcsBitOffset = 32;  // tx_max_mcs_ss1
    constexpr uint8_t kBitWidth = 2;
    uint8_t offset = kMcsBitOffset + (ss_num - 1) * kBitWidth;
    uint64_t mask = ((1ull << kBitWidth) - 1) << offset;
    return (val() & mask) >> offset;
  }

  void set_rx_max_mcs_ss(uint8_t ss_num, uint8_t mcs) {
    ZX_DEBUG_ASSERT(1 <= ss_num && ss_num <= 8);
    constexpr uint8_t kMcsBitOffset = 0;  // rx_max_mcs_ss1
    constexpr uint8_t kBitWidth = 2;
    uint8_t offset = kMcsBitOffset + (ss_num - 1) * kBitWidth;
    uint64_t mcs_val = static_cast<uint64_t>(mcs) << offset;
    set_val(val() | mcs_val);
  }

  void set_tx_max_mcs_ss(uint8_t ss_num, uint8_t mcs) {
    ZX_DEBUG_ASSERT(1 <= ss_num && ss_num <= 8);
    constexpr uint8_t kMcsBitOffset = 32;  // tx_max_mcs_ss1
    constexpr uint8_t kBitWidth = 2;
    uint8_t offset = kMcsBitOffset + (ss_num - 1) * kBitWidth;
    uint64_t mcs_val = static_cast<uint64_t>(mcs) << offset;
    set_val(val() | mcs_val);
  }

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.158
struct VhtCapabilities {
  VhtCapabilitiesInfo vht_cap_info;
  VhtMcsNss vht_mcs_nss;

  static VhtCapabilities FromDdk(const ieee80211_vht_capabilities_t& ddk) {
    VhtCapabilities dst{};
    dst.vht_cap_info.set_val(ddk.vht_capability_info);
    dst.vht_mcs_nss.set_val(ddk.supported_vht_mcs_and_nss_set);
    return dst;
  }

  ieee80211_vht_capabilities_t ToDdk() const {
    ieee80211_vht_capabilities_t ddk{};
    ddk.vht_capability_info = vht_cap_info.val();
    ddk.supported_vht_mcs_and_nss_set = vht_mcs_nss.val();
    return ddk;
  }

} __PACKED;

// IEEE Std 802.11-2016, Figure 9-562
struct BasicVhtMcsNss : public common::BitField<uint16_t> {
 public:
  constexpr explicit BasicVhtMcsNss(uint16_t basic_mcs) : common::BitField<uint16_t>(basic_mcs) {}
  constexpr BasicVhtMcsNss() = default;

  WLAN_BIT_FIELD(ss1, 0, 2)
  WLAN_BIT_FIELD(ss2, 2, 2)
  WLAN_BIT_FIELD(ss3, 4, 2)
  WLAN_BIT_FIELD(ss4, 6, 2)
  WLAN_BIT_FIELD(ss5, 8, 2)
  WLAN_BIT_FIELD(ss6, 10, 2)
  WLAN_BIT_FIELD(ss7, 12, 2)
  WLAN_BIT_FIELD(ss8, 14, 2)

  enum VhtMcsEncoding {
    VHT_MCS_0_TO_7 = 0,
    VHT_MCS_0_TO_8 = 1,
    VHT_MCS_0_TO_9 = 2,
    VHT_MCS_NONE = 3,
  };

  uint8_t get_max_mcs_ss(uint8_t ss_num) const {
    ZX_DEBUG_ASSERT(1 <= ss_num && ss_num <= 8);
    constexpr uint8_t kMcsBitOffset = 0;  // ss1
    constexpr uint8_t kBitWidth = 2;
    uint8_t offset = kMcsBitOffset + (ss_num - 1) * kBitWidth;
    uint64_t mask = ((1ull << kBitWidth) - 1) << offset;
    return (val() & mask) >> offset;
  }

  void set_max_mcs_ss(uint8_t ss_num, uint8_t mcs) {
    ZX_DEBUG_ASSERT(1 <= ss_num && ss_num <= 8);
    constexpr uint8_t kMcsBitOffset = 0;  // ss1
    constexpr uint8_t kBitWidth = 2;
    uint8_t offset = kMcsBitOffset + (ss_num - 1) * kBitWidth;
    uint64_t mcs_val = static_cast<uint64_t>(mcs) << offset;
    set_val(val() | mcs_val);
  }
};

// IEEE Std 802.11-2016, 9.4.2.159
struct VhtOperation {
  uint8_t vht_cbw;
  uint8_t center_freq_seg0;
  uint8_t center_freq_seg1;

  BasicVhtMcsNss basic_mcs;

  enum VhtChannelBandwidth {
    VHT_CBW_20_40 = 0,
    VHT_CBW_80_160_80P80 = 1,
    VHT_CBW_160 = 2,    // Deprecated
    VHT_CBW_80P80 = 3,  // Deprecated

    // 4 - 255 reserved
  };

  static VhtOperation FromDdk(const wlan_vht_op_t& ddk) {
    VhtOperation dst{};
    dst.vht_cbw = ddk.vht_cbw;
    dst.center_freq_seg0 = ddk.center_freq_seg0;
    dst.center_freq_seg1 = ddk.center_freq_seg1;
    dst.basic_mcs.set_val(ddk.basic_mcs);
    return dst;
  }

  wlan_vht_op_t ToDdk() const {
    wlan_vht_op_t dst{};
    dst.vht_cbw = vht_cbw;
    dst.center_freq_seg0 = center_freq_seg0;
    dst.center_freq_seg1 = center_freq_seg1;
    dst.basic_mcs = basic_mcs.val();
    return dst;
  }

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.102
// The fixed part of the Mesh Peering Management header
struct MpmHeader {
  // IEEE Std 802.11-2016, table 9-222
  enum Protocol : uint16_t {
    MPM = 0,
    AMPE = 1,
  };

  Protocol protocol;
  uint16_t local_link_id;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.102
// The optional "PMK" part of the MPM element
struct MpmPmk {
  uint8_t data[16];
} __PACKED;

SupportedMcsSet IntersectMcs(const SupportedMcsSet& lhs, const SupportedMcsSet& rhs);
HtCapabilities IntersectHtCap(const HtCapabilities& lhs, const HtCapabilities& rhs);
VhtCapabilities IntersectVhtCap(const VhtCapabilities& lhs, const VhtCapabilities& rhs);

// Find common legacy rates between AP and client.
// The outcoming "Basic rates" follows those specified in AP
std::vector<SupportedRate> IntersectRatesAp(const std::vector<SupportedRate>& ap_rates,
                                            const std::vector<SupportedRate>& client_rates);
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_ELEMENT_H_
