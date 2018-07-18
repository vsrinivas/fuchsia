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

}  // namespace debug
}  // namespace wlan
