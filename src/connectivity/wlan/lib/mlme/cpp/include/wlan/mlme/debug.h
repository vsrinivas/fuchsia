// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEBUG_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEBUG_H_

#include <wlan/common/tx_vector.h>
#include <wlan/mlme/client/join_context.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/protocol/mac.h>

namespace wlan {
namespace debug {

std::string Describe(const FrameControl& fc);
std::string Describe(const QosControl& qc);
std::string Describe(const LlcHeader& hdr);
std::string Describe(const SequenceControl& sc);
std::string Describe(const MgmtFrameHeader& hdr);
std::string Describe(const DataFrameHeader& hdr);

std::string Describe(const PHY& phy);
std::string Describe(const GI& gi);
std::string Describe(const TxVector& tx_vec, tx_vec_idx_t tx_vec_idx = kInvalidTxVectorIdx);
std::string Describe(tx_vec_idx_t tx_vec_idx);

std::string HexDump(const uint8_t bytes[], size_t bytes_len);
std::string HexDump(Span<const uint8_t> bytes);
std::string HexDumpOneline(Span<const uint8_t> bytes);

std::string Describe(const BlockAckParameters& param);
std::string Describe(const AddBaRequestFrame& req);
std::string Describe(const AddBaResponseFrame& resp);
std::string Describe(const wlan_rx_info_t& rxinfo);
std::string Describe(Packet::Peer peer);
std::string Describe(const Packet& packet);
std::string Describe(const AmsduSubframeHeader& s);

std::string DescribeSuppressed(const Packet& p);

std::string DescribeChannel(const uint8_t arr[], size_t size);
std::string DescribeArray(const uint8_t arr[], size_t size);
std::string DescribeVector(const std::vector<uint8_t> vec);

std::string Describe(const HtCapabilityInfo& hci);
std::string Describe(const AmpduParams& ampdu);
std::string Describe(const SupportedMcsSet& mcs_set);
std::string Describe(const HtExtCapabilities& hec);
std::string Describe(const TxBfCapability& txbf);
std::string Describe(const AselCapability& asel);
std::string Describe(const HtCapabilities& ht_cap);
std::string Describe(const VhtCapabilitiesInfo& vci);
std::string Describe(const VhtMcsNss& vmn);
std::string Describe(const VhtCapabilities& vht_cap);
std::string Describe(const BasicVhtMcsNss& bvmn);
std::string Describe(const VhtOperation& vht_op);

std::string Describe(const wlan_ht_caps& ht_caps);
std::string Describe(const wlan_chan_list& wl);
std::string Describe(const wlan_band_info& bi);
std::string Describe(const wlanmac_info& wi);
std::string Describe(const CapabilityInfo& cap);
std::string Describe(const AssocContext& assoc_ctx);
std::string Describe(const std::vector<SupportedRate> rates);

std::string ToAsciiOrHexStr(const uint8_t bytes[], size_t len);
std::string ToAsciiOrHexStr(const std::vector<uint8_t>& vec);

std::string Describe(const JoinContext& jc);
}  // namespace debug
}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEBUG_H_
