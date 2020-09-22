// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>

namespace wlan {
namespace debug {

const size_t kBytesLenLimit = 16;

// This macro is local within wlan::debug namespace,
// and requires char buf[] and size_t offset variable definitions
// in each function.
#define BUFFER(args...)                                               \
  do {                                                                \
    offset += snprintf(buf + offset, sizeof(buf) - offset, " " args); \
    if (offset >= sizeof(buf)) {                                      \
      snprintf(buf + sizeof(buf) - 12, 12, " ..(trunc)");             \
      offset = sizeof(buf);                                           \
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
  BUFFER("proto:0x%04x", hdr.protocol_id_be);
  return std::string(buf);
}

std::string Describe(const SequenceControl& sc) {
  char buf[40];
  size_t offset = 0;
  BUFFER("frag:%u", sc.frag());
  BUFFER("seq:%u", sc.seq());
  return std::string(buf);
}

std::string Describe(const FrameControl& fc, const common::MacAddr& addr1,
                     const common::MacAddr& addr2, const common::MacAddr& addr3) {
  // TODO(porce): Support A-MSDU case
  char buf[1024];
  size_t offset = 0;
  buf[0] = 0;

  // IEEE Std 802.11-2016, Table 9-26
  uint8_t ds = (fc.to_ds() << 1) + fc.from_ds();
  switch (ds) {
    case 0x0:
      BUFFER("[ra(da)] %s  [ta(sa)] %s  [bssid] %s", addr1.ToString().c_str(),
             addr2.ToString().c_str(), addr3.ToString().c_str());
      break;
    case 0x1:
      BUFFER("[ra(da)] %s  [ta(bssid)] %s  [sa] %s", addr1.ToString().c_str(),
             addr2.ToString().c_str(), addr3.ToString().c_str());
      break;
    case 0x2:
      BUFFER("[ra(bssid)] %s  [ta(sa)] %s  [da] %s", addr1.ToString().c_str(),
             addr2.ToString().c_str(), addr3.ToString().c_str());
      break;
    case 0x3:
      BUFFER("[ra] %s  [ta] %s  [da] %s", addr1.ToString().c_str(), addr2.ToString().c_str(),
             addr3.ToString().c_str());
      break;
    default:
      break;
  }

  return std::string(buf);
}

std::string Describe(const MgmtFrameHeader& hdr) {
  char buf[1024];
  size_t offset = 0;

  BUFFER("[fc] %s dur:%u", Describe(hdr.fc).c_str(), hdr.duration);
  BUFFER("[seq] %s", Describe(hdr.sc).c_str());
  BUFFER("\n        ");
  BUFFER("%s", Describe(hdr.fc, hdr.addr1, hdr.addr2, hdr.addr3).c_str());

  return std::string(buf);
}

std::string Describe(const DataFrameHeader& hdr) {
  char buf[1024];
  size_t offset = 0;

  BUFFER("[fc] %s dur:%u", Describe(hdr.fc).c_str(), hdr.duration);
  BUFFER("[seq] %s", Describe(hdr.sc).c_str());
  BUFFER("\n        ");
  BUFFER("%s", Describe(hdr.fc, hdr.addr1, hdr.addr2, hdr.addr3).c_str());

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

std::string Describe(const wlan_info_phy_type_t& phy) {
  switch (phy) {
    case WLAN_INFO_PHY_TYPE_CCK:
      return "CCK";
    case WLAN_INFO_PHY_TYPE_DSSS:
      return "DSSS";
    case WLAN_INFO_PHY_TYPE_ERP:
      return "ERP";
    case WLAN_INFO_PHY_TYPE_HT:
      return " HT";
    case WLAN_INFO_PHY_TYPE_VHT:
      return "VHT";
    default:
      return "PHY---";
  }
}

std::string Describe(const wlan_gi_t& gi) {
  switch (gi) {
    case WLAN_GI__800NS:
      return "GI800";
    case WLAN_GI__400NS:
      return "GI400";
    case WLAN_GI__200NS:
      return "GI200";
    case WLAN_GI__1600NS:
      return "GI1600";
    case WLAN_GI__3200NS:
      return "GI3200";
    default:
      return "GI---";
  }
}

std::string Describe(const TxVector& tx_vec, tx_vec_idx_t tx_vec_idx) {
  if (tx_vec_idx == kInvalidTxVectorIdx) {
    zx_status_t status = tx_vec.ToIdx(&tx_vec_idx);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  std::ostringstream oss;

  oss << std::setfill(' ');
  oss << std::setw(3) << +tx_vec_idx << ": ";
  oss << Describe(tx_vec.phy) << " ";
  oss << Describe(tx_vec.gi) << " ";
  oss << ::wlan::common::CbwStr(tx_vec.cbw) << " ";
  oss << "NSS " << +tx_vec.nss << " ";
  oss << "MCS " << std::setw(2) << +tx_vec.mcs_idx;
  if (!tx_vec.IsValid()) {
    oss << "(x)";
  }
  return oss.str();
}

std::string Describe(tx_vec_idx_t tx_vec_idx) {
  TxVector tx_vec;
  TxVector::FromIdx(tx_vec_idx, &tx_vec);

  return Describe(tx_vec, tx_vec_idx);
}

std::string DumpToAscii(const uint8_t bytes[], size_t bytes_len) {
  char buf[kBytesLenLimit + 2];
  size_t dump_len = std::min(kBytesLenLimit, bytes_len);
  std::memset(buf, ' ', sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';

  size_t offset = 0;
  for (size_t i = 0; i < kBytesLenLimit; i++) {
    offset = (i < 8) ? i : i + 1;
    if (offset == 8) {
      continue;
    }
    if (i >= dump_len) {
      break;
    }

    buf[offset] = std::isprint(bytes[i]) ? bytes[i] : '.';
  }
  return std::string(buf);
}

std::string HexDump(const uint8_t bytes[], size_t bytes_len) { return HexDump({bytes, bytes_len}); }

std::string HexDump(fbl::Span<const uint8_t> bytes) {
  // Generate a string in following format
  // First 64 of 1500 bytes
  // 0x0000:   b8 ac 6f 2e 57 b3 00 01  6c 99 14 68 08 00 45 10  ..o.W...
  // l..h..E. 0x0010:   00 ec 87 83 40 00 40 06  27 5d ac 10 19 7e ac 10
  // ....@.@. ']...~.. 0x0020:   19 7d 00 16 11 29 d1 2a  af 51 d9 b6 d5 ee 50
  // 18  .}...).* .Q....P. 0x0030:   49 48 8b fa 00 00 0e 12  ea 4d 22 d1 67 c0
  // f1 23  IH...... .M".g..#

  if (bytes.empty()) {
    return "(empty)";
  }

  // TODO(porce): Support other than 64
  const size_t kLenLimit = 400;
  char buf[kLenLimit * 8];
  size_t offset = 0;
  size_t dump_len = std::min(kLenLimit, bytes.size());

  BUFFER("First %zu of %zu bytes\n", dump_len, bytes.size());

  for (size_t line_beg = 0; line_beg < dump_len; line_beg += kBytesLenLimit) {
    size_t line_len = std::min(kBytesLenLimit, dump_len - line_beg);

    BUFFER("0x%04lx:  ", line_beg);
    BUFFER("%s", HexDumpOneline(bytes.subspan(line_beg, line_len)).c_str());

    if (line_beg + kBytesLenLimit < dump_len) {
      BUFFER("\n");  // More lines to print
    }
  }

  return std::string(buf);
}

std::string HexDumpOneline(fbl::Span<const uint8_t> bytes) {
  // Generate a string in following format
  // b8 ac 6f 2e 57 b3 00 01  6c 99 14 68 08 00 45 10  ..o.W... l..h..E.

  if (bytes.empty()) {
    return "";
  }
  char buf[80];
  size_t offset = 0;
  memset(buf, ' ', sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';

  for (size_t i = 0; i < std::min(bytes.size(), kBytesLenLimit); i++) {
    BUFFER("%02x", bytes[i]);
    if (i == 7) {
      BUFFER(" ");
    }
  }
  buf[offset] = ' ';
  offset = 3 * kBytesLenLimit + 2 + 2;  // Fast-forward to align to the ASCII start position
  BUFFER("%s", DumpToAscii(bytes.data(), bytes.size()).c_str());

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
  if (!suppress_msg.empty()) {
    return suppress_msg;
  }

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
      if (auto mgmt_frame = MgmtFrameView<>::CheckType(&p).CheckLength()) {
        BUFFER("\n  wlan hdr:%s ", Describe(*mgmt_frame.hdr()).c_str());
      } else if (auto data_frame = DataFrameView<>::CheckType(&p).CheckLength()) {
        BUFFER("\n  wlan hdr:%s ", Describe(*data_frame.hdr()).c_str());
      }
      break;
    }
    default:
      break;
  }

  BUFFER("\n  packet data: %s", debug::HexDump(p.data(), p.len()).c_str());
  return std::string(buf);
}

std::string Describe(const AmsduSubframeHeader& hdr) {
  char buf[128];
  size_t offset = 0;
  BUFFER("[da] %s [sa] %s [msdu_len] %u", hdr.da.ToString().c_str(), hdr.sa.ToString().c_str(),
         hdr.msdu_len());
  return std::string(buf);
}

std::string DescribeSuppressed(const Packet& p) {
  if (auto bcn_frame = MgmtFrameView<Beacon>::CheckType(&p)) {
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
  for (size_t idx = 0; idx < size; idx++) {
    BUFFER("%02x", arr[idx]);
  }
  return std::string(buf);
}

std::string DescribeVector(const std::vector<uint8_t> vec) {
  char buf[1024];
  buf[0] = 0;
  size_t offset = 0;
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
  BUFFER("rx_stbc:%u", hci.rx_stbc());
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

std::string Describe(const ieee80211_ht_capabilities& ht_caps) {
  return Describe(HtCapabilities::FromDdk(ht_caps));
}

std::string Describe(const VhtCapabilitiesInfo& vci) {
  std::ostringstream oss;
  oss << std::setfill(' ');  // TODO(fxbug.dev/27030): Delete this line
  oss << "max_mpdu_len:" << +vci.max_mpdu_len();
  oss << " supported_cbw_set:" << +vci.supported_cbw_set();
  oss << " rx_ldpc:" << +vci.rx_ldpc();
  oss << " sgi_cbw80:" << +vci.sgi_cbw80();
  oss << " sgi_cbw160:" << +vci.sgi_cbw160();
  oss << " tx_stbc:" << +vci.tx_stbc();
  oss << " rx_stbc:" << +vci.rx_stbc();
  oss << " su_bfer:" << +vci.su_bfer();
  oss << " su_bfee:" << +vci.su_bfee();
  oss << " bfee_sts:" << +vci.bfee_sts();
  oss << " num_sounding:" << +vci.num_sounding();
  oss << " mu_bfer:" << +vci.mu_bfer();
  oss << " mu_bfee:" << +vci.mu_bfee();
  oss << " txop_ps:" << +vci.txop_ps();
  oss << " htc_vht:" << +vci.htc_vht();
  oss << " max_ampdu_exp:" << +vci.max_ampdu_exp();
  oss << " link_adapt:" << +vci.link_adapt();
  oss << " rx_ant_pattern:" << +vci.rx_ant_pattern();
  oss << " tx_ant_pattern:" << +vci.tx_ant_pattern();
  oss << " ext_nss_bw:" << +vci.ext_nss_bw();

  return oss.str();
}

std::string Describe(const VhtMcsNss& vmn) {
  std::ostringstream oss;
  oss << std::setfill(' ');  // TODO(fxbug.dev/27030): Delete this line
  oss << " [rx_max_mcs]";
  oss << " ss1:" << +vmn.rx_max_mcs_ss1();
  oss << " ss2:" << +vmn.rx_max_mcs_ss2();
  oss << " ss3:" << +vmn.rx_max_mcs_ss3();
  oss << " ss4:" << +vmn.rx_max_mcs_ss4();
  oss << " ss5:" << +vmn.rx_max_mcs_ss5();
  oss << " ss6:" << +vmn.rx_max_mcs_ss6();
  oss << " ss7:" << +vmn.rx_max_mcs_ss7();
  oss << " ss8:" << +vmn.rx_max_mcs_ss8();
  oss << " rx_max_data_rate:" << +vmn.rx_max_data_rate();
  oss << " max_nsts:" << +vmn.max_nsts();

  oss << " [tx_max_mcs]";
  oss << " ss1:" << +vmn.tx_max_mcs_ss1();
  oss << " ss2:" << +vmn.tx_max_mcs_ss2();
  oss << " ss3:" << +vmn.tx_max_mcs_ss3();
  oss << " ss4:" << +vmn.tx_max_mcs_ss4();
  oss << " ss5:" << +vmn.tx_max_mcs_ss5();
  oss << " ss6:" << +vmn.tx_max_mcs_ss6();
  oss << " ss7:" << +vmn.tx_max_mcs_ss7();
  oss << " ss8:" << +vmn.tx_max_mcs_ss8();
  oss << " tx_max_data_rate:" << +vmn.tx_max_data_rate();
  oss << " ext_nss_bw:" << +vmn.ext_nss_bw();

  return oss.str();
}

std::string Describe(const VhtCapabilities& vht_cap) {
  std::ostringstream oss;
  oss << std::setfill(' ');  // TODO(fxbug.dev/27030): Delete this line
  oss << "vci:[" << Describe(vht_cap.vht_cap_info).c_str() << "] ";
  oss << "mcs_nss:[" << Describe(vht_cap.vht_mcs_nss).c_str() << "]";
  return oss.str();
}

std::string Describe(const BasicVhtMcsNss& bvmn) {
  std::ostringstream oss;
  oss << std::setfill(' ');  // TODO(fxbug.dev/27030): Delete this line
  oss << " ss1:" << +bvmn.ss1();
  oss << " ss2:" << +bvmn.ss2();
  oss << " ss3:" << +bvmn.ss3();
  oss << " ss4:" << +bvmn.ss4();
  oss << " ss5:" << +bvmn.ss5();
  oss << " ss6:" << +bvmn.ss6();
  oss << " ss7:" << +bvmn.ss7();
  oss << " ss8:" << +bvmn.ss8();
  return oss.str();
}

std::string Describe(const VhtOperation& vht_op) {
  std::ostringstream oss;
  oss << std::setfill(' ');  // TODO(fxbug.dev/27030): Delete this line
  oss << "vht_cbw:" << +vht_op.vht_cbw;
  oss << " center_freq_seg0:" << +vht_op.center_freq_seg0;
  oss << " center_freq_seg1:" << +vht_op.center_freq_seg1;
  oss << " [basic_vht_mcs_nss] " << Describe(vht_op.basic_mcs).c_str();
  return oss.str();
}

std::string Describe(const wlan_info_channel_list& wl) {
  char buf[512];
  size_t offset = 0;
  BUFFER("base_freq:%u", wl.base_freq);
  BUFFER("channels:[%s]", DescribeChannel(wl.channels, 64).c_str());
  return std::string(buf);
}

std::string Describe(const wlan_info_band_info& bi) {
  char buf[1024];
  size_t offset = 0;
  BUFFER("band:%s", common::BandStr(bi.band).c_str());
  BUFFER("ht_caps:[%s]", Describe(bi.ht_caps).c_str());
  BUFFER("vht_supported:%u", bi.vht_supported);
  BUFFER("vht_caps:[to implement]");
  BUFFER("rates:[%s]", DescribeArray(bi.rates, 12).c_str());
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
  BUFFER("#bands:%zu", ii.bands_count);
  for (uint8_t i = 0; i < ii.bands_count; i++) {
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

std::string Describe(const std::vector<SupportedRate> rates) {
  char buf[1024];
  size_t offset = 0;
  buf[0] = 0;
  for (auto const& rate : rates) {
    BUFFER("%s%u", rate.is_basic() ? "*" : "", rate.rate());
  }
  return std::string(buf);
}

bool IsPrint(const uint8_t bytes[], size_t len) {
  for (size_t idx = 0; idx < len; idx++) {
    if (!std::isprint(bytes[idx])) {
      return false;
    }
  }
  return true;
}

std::string ToAsciiOrHexStr(const uint8_t bytes[], size_t len) {
  if (IsPrint(bytes, len)) {
    return std::string(bytes, bytes + len);
  }

  constexpr size_t kMaxLenToPrint = 64;
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t idx = 0; idx < std::min(len, kMaxLenToPrint); ++idx) {
    oss << std::setw(2) << static_cast<unsigned>(bytes[idx]) << " ";
  }

  if (len > kMaxLenToPrint) {
    oss << "..(+" << (kMaxLenToPrint - len) << " bytes)";
  }

  return oss.str();
}

std::string ToAsciiOrHexStr(const std::vector<uint8_t>& vec) {
  return ToAsciiOrHexStr(&vec[0], vec.size());
}

}  // namespace debug
}  // namespace wlan
