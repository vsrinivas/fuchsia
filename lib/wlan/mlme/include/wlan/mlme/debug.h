// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/protocol/mac.h>

namespace wlan {
namespace debug {

std::string Describe(const FrameControl& fc);
std::string Describe(const QosControl& qc);
std::string Describe(const LlcHeader& hdr);
std::string Describe(const SequenceControl& sc);
std::string Describe(const FrameHeader& hdr);
std::string Describe(const MgmtFrameHeader& hdr);
std::string Describe(const DataFrameHeader& hdr);

std::string HexDump(const uint8_t bytes[], size_t bytes_len);
std::string HexDumpOneline(const uint8_t bytes[], size_t bytes_len);

std::string Describe(const BlockAckParameters& param);
std::string Describe(const AddBaRequestFrame& req);
std::string Describe(const AddBaResponseFrame& resp);
std::string Describe(const wlan_rx_info_t& rxinfo);
std::string Describe(Packet::Peer peer);
std::string Describe(const Packet& packet);
std::string Describe(const AmsduSubframe& s);

std::string DescribeSuppressed(const Packet& p);

std::string DescribeArray(const uint8_t arr[], size_t size);
std::string DescribeHtCaps(const wlan_ht_caps& ht_caps);

std::string DescribeSupportedChannels(const wlan_chan_list& wl);
std::string DescribeWlanBandInfo(const wlan_band_info& bi);
std::string DescribeWlanMacInfo(const wlanmac_info& wi);

}  // namespace debug
}  // namespace wlan
