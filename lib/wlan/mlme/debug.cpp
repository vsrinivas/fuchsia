// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/debug.h>

#include <wlan/common/channel.h>
#include <wlan/mlme/mac_frame.h>

#include <cstring>
#include <sstream>
#include <string>

namespace wlan {
namespace debug {

const size_t kBytesLenLimit = 16;

// This macro is local within wlan::debug namespace,
// and requires char buf[] and size_t offset variable defintions
// in each function.
#define BUFFER(args...)                                                   \
    do {                                                                  \
        offset += snprintf(buf + offset, sizeof(buf) - offset, " " args); \
        if (offset >= sizeof(buf)) {                                      \
            snprintf(buf + sizeof(buf) - 12, 12, " ..(trunc)");           \
            offset = sizeof(buf);                                         \
        }                                                                 \
    } while (false)

std::string Describe(const FrameControl& fc) {
    char buf[128];
    size_t offset = 0;

    BUFFER("val:0x%04x", fc.val());
    BUFFER("proto:%u", fc.protocol_version());
    BUFFER("type:%u", fc.type());
    BUFFER("subtype:%u", fc.subtype());
    BUFFER("to_ds:%u", fc.to_ds());
    BUFFER("from_ds:%u", fc.from_ds());
    BUFFER("more_frag:%u", fc.more_frag());
    BUFFER("retry:%u", fc.retry());
    BUFFER("pwr_mgmt:%u", fc.pwr_mgmt());
    BUFFER("more_data:%u", fc.more_data());
    BUFFER("protect:%u", fc.protected_frame());
    BUFFER("htc_order:%u", fc.htc_order());

    return std::string(buf);
}

std::string Describe(const QosControl& qc) {
    char buf[80];
    size_t offset = 0;
    BUFFER("tid:0x%02x", qc.tid());
    BUFFER("eosp:0x%02x", qc.eosp());
    BUFFER("ack_policy:0x%02x", qc.ack_policy());
    BUFFER("amsdu:%u", qc.amsdu_present());
    BUFFER("byte:0x%02x", qc.byte());
    return std::string(buf);
}

std::string Describe(const LlcHeader& hdr) {
    char buf[80];
    size_t offset = 0;
    BUFFER("dsap:0x%02x", hdr.dsap);
    BUFFER("ssap:0x%02x", hdr.ssap);
    BUFFER("ctrl:0x%02x", hdr.control);
    BUFFER("oui:[%02x:%02x:%02x]", hdr.oui[0], hdr.oui[1], hdr.oui[2]);
    BUFFER("proto:0x%04x", hdr.protocol_id);
    return std::string(buf);
}

std::string Describe(const SequenceControl& sc) {
    char buf[40];
    size_t offset = 0;
    BUFFER("frag:%u", sc.frag());
    BUFFER("seq:%u", sc.seq());
    return std::string(buf);
}

std::string Describe(const FrameHeader& hdr) {
    // TODO(porce): Support A-MSDU case
    char buf[1024];
    size_t offset = 0;

    BUFFER("[fc] %s dur:%u", Describe(hdr.fc).c_str(), hdr.duration);
    if (hdr.fc.type() == FrameType::kManagement || hdr.fc.type() == FrameType::kData) {
        BUFFER("[seq] %s", Describe(hdr.sc).c_str());
    }
    BUFFER("\n        ");

    // IEEE Std 802.11-2016, Table 9-26
    uint8_t ds = (hdr.fc.to_ds() << 1) + hdr.fc.from_ds();
    switch (ds) {
    case 0x0:
        BUFFER("[ra(da)] %s  [ta(sa)] %s  [bssid] %s", hdr.addr1.ToString().c_str(),
               hdr.addr2.ToString().c_str(), hdr.addr3.ToString().c_str());
        break;
    case 0x1:
        BUFFER("[ra(da)] %s  [ta(bssid)] %s  [sa] %s", hdr.addr1.ToString().c_str(),
               hdr.addr2.ToString().c_str(), hdr.addr3.ToString().c_str());
        break;
    case 0x2:
        BUFFER("[ra(bssid)] %s  [ta(sa)] %s  [da] %s", hdr.addr1.ToString().c_str(),
               hdr.addr2.ToString().c_str(), hdr.addr3.ToString().c_str());
        break;
    case 0x3:
        BUFFER("[ra] %s  [ta] %s  [da] %s", hdr.addr1.ToString().c_str(),
               hdr.addr2.ToString().c_str(), hdr.addr3.ToString().c_str());
        break;
    default:
        break;
    }

    return std::string(buf);
}

std::string Describe(const MgmtFrameHeader& hdr) {
    char buf[1024];
    size_t offset = 0;
    BUFFER("%s", Describe(*reinterpret_cast<const FrameHeader*>(&hdr)).c_str());

    return std::string(buf);
}

std::string Describe(const DataFrameHeader& hdr) {
    char buf[1024];
    size_t offset = 0;
    BUFFER("%s", Describe(*reinterpret_cast<const FrameHeader*>(&hdr)).c_str());

    if (hdr.HasAddr4()) {
        ZX_DEBUG_ASSERT(hdr.addr4() != nullptr);
        BUFFER("[addr4] %s", hdr.addr4()->ToString().c_str());
    }
    if (hdr.HasQosCtrl()) {
        ZX_DEBUG_ASSERT(hdr.qos_ctrl() != nullptr);
        BUFFER("\n        qos_ctrl: %s", Describe(*hdr.qos_ctrl()).c_str());
    }

    return std::string(buf);
}

std::string DumpToAscii(const uint8_t bytes[], size_t bytes_len) {
    char buf[kBytesLenLimit + 2];
    size_t dump_len = std::min(kBytesLenLimit, bytes_len);
    std::memset(buf, ' ', sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    size_t offset = 0;
    for (size_t i = 0; i < kBytesLenLimit; i++) {
        offset = (i < 8) ? i : i + 1;
        if (offset == 8) { continue; }
        if (i >= dump_len) { break; }

        buf[offset] = std::isprint(bytes[i]) ? bytes[i] : '.';
    }
    return std::string(buf);
}

std::string HexDump(const uint8_t bytes[], size_t bytes_len) {
    // Generate a string in following format
    // First 64 of 1500 bytes
    // 0x0000:   b8 ac 6f 2e 57 b3 00 01  6c 99 14 68 08 00 45 10  ..o.W... l..h..E.
    // 0x0010:   00 ec 87 83 40 00 40 06  27 5d ac 10 19 7e ac 10  ....@.@. ']...~..
    // 0x0020:   19 7d 00 16 11 29 d1 2a  af 51 d9 b6 d5 ee 50 18  .}...).* .Q....P.
    // 0x0030:   49 48 8b fa 00 00 0e 12  ea 4d 22 d1 67 c0 f1 23  IH...... .M".g..#

    if (bytes == nullptr || bytes_len == 0) { return "(empty)"; }

    // TODO(porce): Support other than 64
    const size_t kLenLimit = 400;
    char buf[kLenLimit * 8];
    size_t offset = 0;
    size_t dump_len = std::min(kLenLimit, bytes_len);

    BUFFER("First %zu of %zu bytes\n", dump_len, bytes_len);

    for (size_t line_beg = 0; line_beg < dump_len; line_beg += kBytesLenLimit) {
        size_t line_len = std::min(kBytesLenLimit, dump_len - line_beg);

        BUFFER("0x%04lx:  ", line_beg);
        BUFFER("%s", HexDumpOneline(&bytes[line_beg], line_len).c_str());

        if (line_beg + kBytesLenLimit < dump_len) {
            BUFFER("\n");  // More lines to print
        }
    }

    return std::string(buf);
}

std::string HexDumpOneline(const uint8_t bytes[], size_t bytes_len) {
    // Generate a string in following format
    // b8 ac 6f 2e 57 b3 00 01  6c 99 14 68 08 00 45 10  ..o.W... l..h..E.

    if (bytes == nullptr || bytes_len == 0) { return ""; }
    char buf[80];
    size_t offset = 0;
    memset(buf, ' ', sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    for (size_t i = 0; i < std::min(bytes_len, kBytesLenLimit); i++) {
        BUFFER("%02x", bytes[i]);
        if (i == 7) { BUFFER(" "); }
    }
    buf[offset] = ' ';
    offset = 3 * kBytesLenLimit + 2 + 2;  // Fast-forward to align to the ASCII start position
    BUFFER("%s", DumpToAscii(bytes, bytes_len).c_str());

    return std::string(buf);
}

std::string Describe(const BlockAckParameters& params) {
    char buf[128];
    size_t offset = 0;
    BUFFER("amsdu:%u", params.amsdu());
    BUFFER("policy:%u", params.policy());
    BUFFER("tid:0x%x", params.tid());
    BUFFER("buffer_size:%u", params.buffer_size());
    return std::string(buf);
}

std::string Describe(const AddBaRequestFrame& req) {
    char buf[256];
    size_t offset = 0;
    BUFFER("addbareq frame:");
    BUFFER("dialog_token:0x%02x", req.dialog_token);
    BUFFER("params: %s", Describe(req.params).c_str());
    BUFFER("timeout:%u", req.timeout);
    BUFFER("fragment:%u", req.seq_ctrl.fragment());
    BUFFER("starting_seq:%u", req.seq_ctrl.starting_seq());
    return std::string(buf);
}

std::string Describe(const AddBaResponseFrame& resp) {
    char buf[256];
    size_t offset = 0;
    BUFFER("addbaresp frame:");
    BUFFER("dialog_token:0x%02x", resp.dialog_token);
    BUFFER("status_code:%u", resp.status_code);
    BUFFER("params: %s", Describe(resp.params).c_str());
    BUFFER("timeout:%u", resp.timeout);
    return std::string(buf);
}

std::string Describe(const wlan_rx_info_t& rxinfo) {
    char buf[256];
    size_t offset = 0;
    BUFFER("flags:0x%0x8", rxinfo.rx_flags);
    BUFFER("valid_fields:0x%0x8", rxinfo.valid_fields);
    BUFFER("phy:%u", rxinfo.phy);
    BUFFER("data_rate:%u", rxinfo.data_rate);
    BUFFER("chan:%s", common::ChanStr(rxinfo.chan).c_str());
    BUFFER("mcs:%u", rxinfo.mcs);
    BUFFER("rssi_dbm:%d", rxinfo.rssi_dbm);
    BUFFER("rcpi_dbmh:%d", rxinfo.rcpi_dbmh);
    BUFFER("snr_dbh:%d", rxinfo.snr_dbh);
    return std::string(buf);
}

std::string Describe(Packet::Peer peer) {
    switch (peer) {
    case Packet::Peer::kUnknown:
        return "Unknown";
    case Packet::Peer::kDevice:
        return "Device";
    case Packet::Peer::kWlan:
        return "WLAN";
    case Packet::Peer::kEthernet:
        return "Ethernet";
    case Packet::Peer::kService:
        return "Service";
    default:
        return "Undefined";
    }
}

std::string Describe(const Packet& p) {
    std::string suppress_msg = DescribeSuppressed(p);
    if (!suppress_msg.empty()) { return suppress_msg; }

    char buf[2048];
    size_t offset = 0;
    auto has_rxinfo = p.has_ctrl_data<wlan_rx_info_t>();

    BUFFER("len:%zu", p.len());
    BUFFER("peer:%s", Describe(p.peer()).c_str());
    BUFFER("has_ext_data:%u", p.has_ext_data());
    BUFFER("ext_offset:%u", p.ext_offset());
    BUFFER("has_rxinfo:%u", has_rxinfo);

    if (has_rxinfo) {
        auto rxinfo = p.ctrl_data<wlan_rx_info_t>();
        BUFFER("\n  rxinfo:%s", Describe(*rxinfo).c_str());
    }

    switch (p.peer()) {
    case Packet::Peer::kWlan: {
        auto hdr = p.field<FrameHeader>(0);
        if (hdr->fc.type() == FrameType::kManagement || hdr->fc.type() == FrameType::kData) {
            BUFFER("\n  wlan hdr:%s ", Describe(*hdr).c_str());
        }
        break;
    }
    default:
        break;
    }

    BUFFER("\n  packet data: %s", debug::HexDump(p.data(), p.len()).c_str());
    return std::string(buf);
}

std::string Describe(const AmsduSubframe& s) {
    char buf[128];
    size_t offset = 0;
    BUFFER("[da] %s [sa] %s [msdu_len] %u", s.hdr.da.ToString().c_str(),
           s.hdr.sa.ToString().c_str(), s.hdr.msdu_len());
    return std::string(buf);
}

std::string DescribeSuppressed(const Packet& p) {
    auto hdr = p.field<FrameHeader>(0);

    if (hdr->fc.type() == FrameType::kManagement &&
        hdr->fc.subtype() == ManagementSubtype::kBeacon) {
        return "Beacon. Decoding suppressed.";
    }

    return "";
}

std::string DescribeChannel(const uint8_t arr[], size_t size) {
    char buf[1024];
    size_t offset = 0;
    buf[0] = 0;
    for (size_t idx = 0; idx < size && arr[idx] != 0; idx++) {
        BUFFER("%u", arr[idx]);
    }
    return std::string(buf);
}

std::string DescribeArray(const uint8_t arr[], size_t size) {
    char buf[1024];
    buf[0] = 0;
    size_t offset = 0;
    buf[0] = 0;
    for (size_t idx = 0; idx < size; idx++) {
        BUFFER("%02x", arr[idx]);
    }
    return std::string(buf);
}

std::string DescribeVector(const std::vector<uint8_t> vec) {
    char buf[1024];
    buf[0] = 0;
    size_t offset = 0;
    buf[0] = 0;
    for (auto const& v : vec) {
        BUFFER("%02x", v);
    }
    return std::string(buf);
}

std::string Describe(const HtCapabilityInfo& hci) {
    char buf[256];
    size_t offset = 0;
    BUFFER("ldcp:%u", hci.ldpc_coding_cap());
    BUFFER("chanwidth:%u", hci.chan_width_set());
    BUFFER("smps:%u", hci.sm_power_save());
    BUFFER("gf:%u", hci.greenfield());
    BUFFER("sgi20:%u", hci.short_gi_20());
    BUFFER("sgi40:%u", hci.short_gi_40());
    BUFFER("tx_stbc:%u", hci.tx_stbc());
    BUFFER("delayed_back:%u", hci.delayed_block_ack());
    BUFFER("max_amsdu_len:%u", hci.max_amsdu_len());
    BUFFER("dsss40:%u", hci.dsss_in_40());
    BUFFER("int40:%u", hci.intolerant_40());
    BUFFER("lsig_txop:%u", hci.lsig_txop_protect());
    return std::string(buf);
}

std::string Describe(const AmpduParams& ampdu) {
    char buf[128];
    size_t offset = 0;
    BUFFER("exp:%u", ampdu.exponent());
    BUFFER("min_start:%u", ampdu.min_start_spacing());
    return std::string(buf);
}

std::string Describe(const SupportedMcsSet& mcs_set) {
    char buf[256];
    size_t offset = 0;
    BUFFER("rx1:0x%016lx", mcs_set.rx_mcs_head.bitmask());
    BUFFER("rx2:0x%04x", mcs_set.rx_mcs_tail.bitmask());
    BUFFER("high_rate:%u", mcs_set.rx_mcs_tail.highest_rate());
    BUFFER("tx_set:%u", mcs_set.tx_mcs.set_defined());
    BUFFER("tx_rx_diff:%u", mcs_set.tx_mcs.rx_diff());
    BUFFER("max_ss:%u", mcs_set.tx_mcs.max_ss());
    BUFFER("ueqm:%u", mcs_set.tx_mcs.ueqm());
    return std::string(buf);
}

std::string Describe(const HtExtCapabilities& hec) {
    char buf[256];
    size_t offset = 0;
    BUFFER("pco:%u", hec.pco());
    BUFFER("pco_trans:%u", hec.pco_transition());
    BUFFER("mcs_feedback:%u", hec.mcs_feedback());
    BUFFER("htc_ht:%u", hec.htc_ht_support());
    BUFFER("rd:%u", hec.rd_responder());
    return std::string(buf);
}

std::string Describe(const TxBfCapability& txbf) {
    char buf[512];
    size_t offset = 0;
    BUFFER("imp_rx:%u", txbf.implicit_rx());
    BUFFER("rx_stag:%u", txbf.rx_stag_sounding());
    BUFFER("tx_stag:%u", txbf.tx_stag_sounding());
    BUFFER("rx_ndp:%u", txbf.rx_ndp());
    BUFFER("tx_ndp:%u", txbf.tx_ndp());
    BUFFER("imp:%u", txbf.implicit());
    BUFFER("cal:%u", txbf.calibration());
    BUFFER("csi:%u", txbf.csi());
    BUFFER("noncomp_steer:%u", txbf.noncomp_steering());
    BUFFER("comp_steer:%u", txbf.comp_steering());
    BUFFER("min_grp:%u", txbf.min_grouping());
    BUFFER("csi_ant:%u", txbf.csi_antennas());
    BUFFER("noncomp_steer:%u", txbf.noncomp_steering_ants());
    BUFFER("comp_steer:%u", txbf.comp_steering_ants());
    BUFFER("csi_rows:%u", txbf.csi_rows());
    BUFFER("est:%u", txbf.chan_estimation());
    return std::string(buf);
}

std::string Describe(const AselCapability& asel) {
    char buf[512];
    size_t offset = 0;
    BUFFER("asel:%u", asel.asel());
    BUFFER("csi_tx:%u", asel.csi_feedback_tx_asel());
    BUFFER("ant_idx_tx:%u", asel.ant_idx_feedback_tx_asel());
    BUFFER("exp_csi:%u", asel.explicit_csi_feedback());
    BUFFER("ant_idx:%u", asel.antenna_idx_feedback());
    BUFFER("rx_asel:%u", asel.rx_asel());
    BUFFER("tx_sound:%u", asel.tx_sounding_ppdu());
    return std::string(buf);
}

std::string Describe(const HtCapabilities& ht_cap) {
    char buf[2048];
    size_t offset = 0;
    BUFFER("hci:[%s]", Describe(ht_cap.ht_cap_info).c_str());
    BUFFER("ampdu:[%s]", Describe(ht_cap.ampdu_params).c_str());
    BUFFER("mcs_set:[%s]", Describe(ht_cap.mcs_set).c_str());
    BUFFER("ext_cap:[%s]", Describe(ht_cap.ht_ext_cap).c_str());
    BUFFER("txbf_cap:[%s]", Describe(ht_cap.txbf_cap).c_str());
    BUFFER("asel_cap:[%s]", Describe(ht_cap.asel_cap).c_str());
    return std::string(buf);
}

std::string Describe(const wlan_ht_caps& ht_caps) {
    HtCapabilities ht_cap{};
    static_assert(sizeof(ht_caps) + sizeof(ElementHeader) == sizeof(ht_cap),
                  "DDK struct and IEEE IE struct size mismatch");
    auto elem = reinterpret_cast<uint8_t*>(&ht_cap);

    // wlan_ht_caps is a packed struct.
    memcpy(elem + sizeof(ElementHeader), &ht_caps, sizeof(ht_caps));
    return Describe(ht_cap);
}

std::string Describe(const wlan_chan_list& wl) {
    char buf[512];
    size_t offset = 0;
    BUFFER("base_freq:%u", wl.base_freq);
    BUFFER("channels:[%s]", DescribeChannel(wl.channels, 64).c_str());
    return std::string(buf);
}

std::string Describe(const wlan_band_info& bi) {
    char buf[1024];
    size_t offset = 0;
    BUFFER("desc:%s", bi.desc);
    BUFFER("ht_caps:[%s]", Describe(bi.ht_caps).c_str());
    BUFFER("vht_supported:%u", bi.vht_supported);
    BUFFER("vht_caps:[to implement]");
    BUFFER("basic_rates:[%s]", DescribeArray(bi.basic_rates, 12).c_str());
    BUFFER("supported_channels:[%s]", Describe(bi.supported_channels).c_str());
    return std::string(buf);
}

std::string Describe(const wlanmac_info& wi) {
    char buf[2048];
    size_t offset = 0;

    auto& ii = wi.ifc_info;
    BUFFER("mac:[%s]", common::MacAddr(ii.mac_addr).ToString().c_str());
    BUFFER("role:%u", ii.mac_role);
    BUFFER("phys:0x%04x", ii.supported_phys);
    BUFFER("feat:0x%08x", ii.driver_features);
    BUFFER("cap:0x%08x", ii.caps);
    BUFFER("#bands:%u", ii.num_bands);
    for (uint8_t i = 0; i < ii.num_bands; i++) {
        BUFFER("[band %u]%s", i, Describe(ii.bands[i]).c_str());
    }
    return std::string(buf);
}

std::string Describe(const CapabilityInfo& cap) {
    char buf[512];
    size_t offset = 0;
    BUFFER("ess:%u", cap.ess());
    BUFFER("ibss:%u", cap.ibss());
    BUFFER("cf_pollable:%u", cap.cf_pollable());
    BUFFER("cf_poll_req:%u", cap.cf_poll_req());
    BUFFER("privacy:%u", cap.privacy());
    BUFFER("short_preamble:%u", cap.short_preamble());
    BUFFER("spectrum_mgmt:%u", cap.spectrum_mgmt());
    BUFFER("qos:%u", cap.qos());
    BUFFER("short_slot_time:%u", cap.short_slot_time());
    BUFFER("apsd:%u", cap.apsd());
    BUFFER("radio_msmt:%u", cap.radio_msmt());
    BUFFER("delayed_block_ack:%u", cap.delayed_block_ack());
    BUFFER("immediate_block_ack:%u", cap.immediate_block_ack());
    return std::string(buf);
}

std::string Describe(const AssocContext& assoc_ctx) {
    char buf[2048];
    size_t offset = 0;
    BUFFER("bssid:[%s]", assoc_ctx.bssid.ToString().c_str());
    BUFFER("aid:%u", assoc_ctx.aid);
    BUFFER("cap:[%s]", Describe(assoc_ctx.cap).c_str());
    BUFFER("supp_rates:[%s]", DescribeVector(assoc_ctx.supported_rates).c_str());
    BUFFER("ext_supp_rates:[%s]", DescribeVector(assoc_ctx.ext_supported_rates).c_str());

    // TODO(NET-1278): Show HT / VHT capabilities

    return std::string(buf);
}

}  // namespace debug
}  // namespace wlan
