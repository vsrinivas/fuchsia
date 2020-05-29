// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_STRUCTS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_STRUCTS_H_

// This file contains structure definitions for interfacing with the hardware DMA queues used by the
// Broadcom chip.

namespace wlan {
namespace brcmfmac {

struct [[gnu::packed]] MsgbufCommonHeader {
  enum class MsgType : uint8_t {
    kInvalid = 0,
    kIoctlRequest = 0x09,
    kIoctlAck = 0x0A,
    kIoctlBufferPost = 0xB,
    kIoctlResponse = 0x0C,
    kEventBufferPost = 0x0D,
    kRxBufferPost = 0x11,
  };

  MsgType msgtype;
  uint8_t ifidx;
  uint8_t flags;
  uint8_t rsvd0;
  uint32_t request_id;
};

struct [[gnu::packed]] MsgbufCompletionHeader {
  int16_t status;  // Note: signed integer.
  uint16_t flow_ring_id;
};

struct [[gnu::packed]] MsgbufIoctlRequest {
  static constexpr MsgbufCommonHeader::MsgType kMsgType =
      MsgbufCommonHeader::MsgType::kIoctlRequest;
  MsgbufCommonHeader msg;
  uint32_t cmd;
  uint16_t trans_id;
  uint16_t input_buf_len;
  uint16_t output_buf_len;
  uint16_t rsvd0[3];
  uint64_t req_buf_addr;
  uint32_t rsvd1[2];
};

struct [[gnu::packed]] MsgbufIoctlOrEventBufferPost {
  // This struct may have have a MsgType of either kIoctlBufferPost or kEventBufferPost.
  MsgbufCommonHeader msg;
  uint16_t host_buf_len;
  uint16_t rsvd0[3];
  uint64_t host_buf_addr;
  uint32_t rsvd1[4];
};

struct [[gnu::packed]] MsgbufRxBufferPost {
  static constexpr MsgbufCommonHeader::MsgType kMsgType =
      MsgbufCommonHeader::MsgType::kRxBufferPost;
  MsgbufCommonHeader msg;
  uint16_t metadata_buf_len;
  uint16_t data_buf_len;
  uint32_t rsvd0;
  uint64_t metadata_buf_addr;
  uint64_t data_buf_addr;
};

struct [[gnu::packed]] MsgbufIoctlResponse {
  static constexpr MsgbufCommonHeader::MsgType kMsgType =
      MsgbufCommonHeader::MsgType::kIoctlResponse;
  MsgbufCommonHeader msg;
  MsgbufCompletionHeader compl_hdr;
  uint16_t resp_len;
  uint16_t trans_id;
  uint32_t cmd;
  uint32_t rsvd0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_STRUCTS_H_
