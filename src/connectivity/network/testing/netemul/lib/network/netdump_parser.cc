// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netdump_parser.h"

#include <src/lib/fxl/logging.h>

#include "netdump_types.h"

#define INSUFF_LEN(a, b)                                                 \
  if ((a) < (b)) {                                                       \
    FXL_LOG(ERROR) << "Insufficient length " << (a) << ", need " << (b); \
    return false;                                                        \
  }

namespace netemul {

namespace testing {

bool NetDumpParser::Parse(const uint8_t* data, size_t len) {
  interfaces_.clear();
  packets_.clear();

  INSUFF_LEN(len, sizeof(pcap_shb_t));

  pcap_shb_t shb;
  memcpy(&shb, data, sizeof(shb));
  data += sizeof(shb);
  len -= sizeof(shb);
  // expect the first block to be a section block
  if (shb.magic != kSectionHeaderByteOrderMagic ||
      shb.blk_tot_len != sizeof(shb) || shb.blk_tot_len2 != sizeof(shb) ||
      shb.type != kSectionHeaderBlockType) {
    FXL_LOG(ERROR) << "Invalid section header block";
    return false;
  }
  // now read all the rest of the blocks:

  while (len > 0) {
    struct {
      uint32_t type;
      uint32_t len;
    } __attribute__((packed)) block_header{};

    INSUFF_LEN(len, sizeof(block_header));

    memcpy(&block_header, data, sizeof(block_header));

    INSUFF_LEN(len, block_header.len);

    // at the end of every block, we repeat the size:
    uint32_t len2;
    memcpy(&len2, data + block_header.len - sizeof(len2), sizeof(len2));
    // end-of-block length doesn't match:
    if (len2 != block_header.len) {
      FXL_LOG(ERROR) << "Block header and footer don't match lengths";
      return false;
    }

    if (block_header.type == kInterfaceDescriptionBlockType) {
      pcap_idb_t idb;

      INSUFF_LEN(len, sizeof(idb));

      memcpy(&idb, data, sizeof(idb));
      // only link type we know:
      if (idb.linktype != kLinkTypeEthernet) {
        FXL_LOG(ERROR) << "Unrecognized link type: " << idb.linktype;
        return false;
      }

      auto options = data + sizeof(idb);
      // length of options is the rest of the block,
      // removing the header and the end of block length
      uint32_t options_len = idb.blk_tot_len - sizeof(idb) - sizeof(uint32_t);

      while (options_len > 0) {
        option_tlv_t option_tlv;
        INSUFF_LEN(options_len, sizeof(option_tlv));

        memcpy(&option_tlv, options, sizeof(option_tlv));
        auto padded_len = PAD(static_cast<uint32_t>(option_tlv.len));

        INSUFF_LEN(options_len, padded_len);

        if (option_tlv.type == kOptionInterfaceName) {
          interfaces_.emplace_back(
              options + sizeof(option_tlv_t),
              options + sizeof(option_tlv_t) + option_tlv.len);
        } else {
          // we don't recognize any other types of options.
          FXL_LOG(ERROR) << "Unrecognized interface definition option: "
                         << option_tlv.type;
          return false;
        }

        padded_len += sizeof(option_tlv);
        options_len -= padded_len;
        options += padded_len;
      }

    } else if (block_header.type == kEnhancedPacketBlockType) {
      INSUFF_LEN(len, sizeof(enhanced_pkt_t));

      enhanced_pkt_t pkt;
      memcpy(&pkt, data, sizeof(pkt));

      if (
          // we always put capture and orig len on the same value:
          pkt.captured_len != pkt.orig_len
          // check that the reported size matches the external block
          // information:
          ||
          PAD(pkt.captured_len) + sizeof(enhanced_pkt_t) + sizeof(uint32_t) !=
              block_header.len
          // check that the interface is actually specified in interfaces:
          || pkt.interface_id >= interfaces_.size()) {
        FXL_LOG(ERROR) << "Invalid enhanced packet block";
        return false;
      }
      packets_.push_back(
          ParsedPacket{data + sizeof(pkt), pkt.captured_len, pkt.interface_id});
    } else {
      FXL_LOG(ERROR) << "Unrecognized block type: " << block_header.type;
      return false;
    }
    data += len2;
    len -= len2;
  }

  return true;
}

}  // namespace testing

}  // namespace netemul
