// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"

#include "garnet/drivers/bluetooth/lib/common/packet_view.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/sdp/status.h"

#include <endian.h>

namespace btlib {

using common::BufferView;
using common::ByteBuffer;
using common::MutableByteBuffer;

namespace sdp {

namespace {

// Validates continuation state in |buf|, which should be the configuration
// state bytes of a PDU.
// Returns true if the continuation state is valid here, false otherwise.
// Sets |out| to point to it if present and valid.
bool ValidContinuationState(const ByteBuffer& buf, BufferView* out) {
  FXL_DCHECK(out);
  if (buf.size() == 0) {
    return false;
  }
  uint8_t len = buf[0];
  if (len == 0) {
    *out = BufferView();
    return true;
  }
  if (len >= kMaxContStateLength || len > (buf.size() - 1)) {
    return false;
  }
  *out = buf.view(1, len);
  return true;
}

common::MutableByteBufferPtr GetNewPDU(OpCode pdu_id, TransactionId tid,
                                       uint16_t param_length) {
  auto ptr = common::NewSlabBuffer(sizeof(Header) + param_length);
  if (!ptr) {
    return nullptr;
  }
  common::MutablePacketView<Header> packet(ptr.get(), param_length);
  packet.mutable_header()->pdu_id = pdu_id;
  packet.mutable_header()->tid = htobe16(tid);
  packet.mutable_header()->param_length = htobe16(param_length);
  return ptr;
}

}  // namespace

Request::Request() { cont_state_.Fill(0); }

void Request::SetContinuationState(const ByteBuffer& buf) {
  FXL_DCHECK(buf.size() < kMaxContStateLength);
  cont_state_[0] = buf.size();
  if (cont_state_[0] == 0) {
    return;
  }
  size_t copied = buf.Copy(&cont_state_, sizeof(uint8_t), buf.size());
  FXL_DCHECK(copied == buf.size());
}

bool Request::ParseContinuationState(const ByteBuffer& buf) {
  BufferView view;
  if (!ValidContinuationState(buf, &view)) {
    return false;
  }
  SetContinuationState(view);
  return true;
}

size_t Request::WriteContinuationState(MutableByteBuffer* buf) const {
  FXL_DCHECK(buf->size() > cont_info_size());
  size_t written_size = sizeof(uint8_t) + cont_info_size();
  buf->Write(cont_state_.view(0, written_size));
  return written_size;
}

Status ErrorResponse::Parse(const ByteBuffer& buf) {
  if (complete()) {
    return Status(common::HostError::kNotReady);
  }
  if (buf.size() != sizeof(ErrorCode)) {
    return Status(common::HostError::kPacketMalformed);
  }
  error_code_ = ErrorCode(betoh16(buf.As<uint16_t>()));
  return Status();
}

common::MutableByteBufferPtr ErrorResponse::GetPDU(uint16_t, TransactionId tid,
                                                   const ByteBuffer&) const {
  auto ptr = GetNewPDU(kErrorResponse, tid, sizeof(ErrorCode));
  size_t written = sizeof(Header);

  uint16_t err = htobe16(static_cast<uint16_t>(error_code_));
  ptr->Write(reinterpret_cast<uint8_t*>(&err), sizeof(uint16_t), written);

  return ptr;
}

}  // namespace sdp

}  // namespace btlib
