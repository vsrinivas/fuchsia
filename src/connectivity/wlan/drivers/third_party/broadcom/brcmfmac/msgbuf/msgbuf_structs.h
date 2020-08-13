// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_STRUCTS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_STRUCTS_H_

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"

// This file contains structure definitions for interfacing with the hardware DMA queues used by the
// Broadcom chip.

namespace wlan {
namespace brcmfmac {

struct [[gnu::packed]] MsgbufCommonHeader {
  enum class MsgType : uint8_t {
    kInvalid = 0,
    kFlowRingCreateRequest = 0x03,
    kFlowRingCreateResponse = 0x04,
    kFlowRingDeleteRequest = 0x05,
    kFlowRingDeleteResponse = 0x06,
    kIoctlRequest = 0x09,
    kIoctlAck = 0x0A,
    kIoctlBufferPost = 0xB,
    kIoctlResponse = 0x0C,
    kEventBufferPost = 0x0D,
    kWlEvent = 0x0E,
    kTxRequest = 0x0F,
    kTxResponse = 0x10,
    kRxBufferPost = 0x11,
  };

  MsgType msgtype;
  uint8_t ifidx;
  uint8_t flags;
  uint8_t rsvd0;
  uint32_t request_id;
};

// The status field is an int16_t which can be safely cast to a bcme_status_t.
// The type alias bcme_short_status is used as a reminder of this. Note that
// we must ensure the size of bcme_short_status is the same as int16_t because
// int16_t is the original type of the MsgbufCompletionHeader status field.
static_assert(sizeof(bcme_short_status_t) == sizeof(int16_t));
struct [[gnu::packed]] MsgbufCompletionHeader {
  bcme_short_status_t status;  // Note: signed integer.
  uint16_t flow_ring_id;
};

struct [[gnu::packed]] MsgbufFlowRingCreateRequest {
  static constexpr MsgbufCommonHeader::MsgType kMsgType =
      MsgbufCommonHeader::MsgType::kFlowRingCreateRequest;
  MsgbufCommonHeader msg;
  uint8_t da[6];
  uint8_t sa[6];
  uint8_t tid;
  uint8_t if_flags;
  uint16_t flow_ring_id;
  uint8_t tc;
  uint8_t priority;
  uint16_t int_vector;
  uint16_t max_items;
  uint16_t len_item;
  uint64_t flow_ring_addr;
};

struct [[gnu::packed]] MsgbufFlowRingDeleteRequest {
  static constexpr MsgbufCommonHeader::MsgType kMsgType =
      MsgbufCommonHeader::MsgType::kFlowRingDeleteRequest;
  MsgbufCommonHeader msg;
  uint16_t flow_ring_id;
  uint16_t reason;
  uint32_t rsvd0[7];
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

struct [[gnu::packed]] MsgbufTxRequest {
  static constexpr MsgbufCommonHeader::MsgType kMsgType = MsgbufCommonHeader::MsgType::kTxRequest;
  MsgbufCommonHeader msg;
  uint8_t txhdr[14];
  uint8_t flags;
  uint8_t seg_cnt;
  uint64_t metadata_buf_addr;
  uint64_t data_buf_addr;
  uint16_t metadata_buf_len;
  uint16_t data_len;
  uint32_t rsvd0;
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

struct [[gnu::packed]] MsgbufFlowRingCreateResponse {
  static constexpr MsgbufCommonHeader::MsgType kMsgType =
      MsgbufCommonHeader::MsgType::kFlowRingCreateResponse;
  MsgbufCommonHeader msg;
  MsgbufCompletionHeader compl_hdr;
  uint32_t rsvd0[3];
};

struct [[gnu::packed]] MsgbufFlowRingDeleteResponse {
  static constexpr MsgbufCommonHeader::MsgType kMsgType =
      MsgbufCommonHeader::MsgType::kFlowRingDeleteResponse;
  MsgbufCommonHeader msg;
  MsgbufCompletionHeader compl_hdr;
  uint32_t rsvd0[3];
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

struct [[gnu::packed]] MsgbufTxResponse {
  static constexpr MsgbufCommonHeader::MsgType kMsgType = MsgbufCommonHeader::MsgType::kTxResponse;
  MsgbufCommonHeader msg;
  MsgbufCompletionHeader compl_hdr;
  uint16_t metadata_len;
  uint16_t tx_status;
};

struct [[gnu::packed]] MsgbufWlEvent {
  static constexpr MsgbufCommonHeader::MsgType kMsgType = MsgbufCommonHeader::MsgType::kWlEvent;
  MsgbufCommonHeader msg;
  MsgbufCompletionHeader compl_hdr;
  uint16_t event_data_len;
  uint16_t seqnum;
  uint16_t rsvd0[4];
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_STRUCTS_H_
