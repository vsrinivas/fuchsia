// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/packet_view.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/sdp/status.h"

#include <endian.h>

namespace btlib {

using common::BufferView;
using common::ByteBuffer;
using common::HostError;
using common::MutableByteBuffer;

namespace sdp {

namespace {

constexpr size_t kMaxServiceSearchSize = 12;

// Validates continuation state in |buf|, which should be the configuration
// state bytes of a PDU.
// Returns true if the continuation state is valid here, false otherwise.
// Sets |out| to point to it if present and valid.
bool ValidContinuationState(const ByteBuffer& buf, BufferView* out) {
  ZX_DEBUG_ASSERT(out);
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
  ZX_DEBUG_ASSERT(buf.size() < kMaxContStateLength);
  cont_state_[0] = buf.size();
  if (cont_state_[0] == 0) {
    return;
  }
  size_t copied = buf.Copy(&cont_state_, sizeof(uint8_t), buf.size());
  ZX_DEBUG_ASSERT(copied == buf.size());
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
  ZX_DEBUG_ASSERT(buf->size() > cont_info_size());
  size_t written_size = sizeof(uint8_t) + cont_info_size();
  buf->Write(cont_state_.view(0, written_size));
  return written_size;
}

Status ErrorResponse::Parse(const ByteBuffer& buf) {
  if (complete()) {
    return Status(HostError::kNotReady);
  }
  if (buf.size() != sizeof(ErrorCode)) {
    return Status(HostError::kPacketMalformed);
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

ServiceSearchRequest::ServiceSearchRequest()
    : Request(), max_service_record_count_(0xFFFF) {}

ServiceSearchRequest::ServiceSearchRequest(const common::ByteBuffer& params)
    : ServiceSearchRequest() {
  DataElement search_pattern;
  size_t read_size = DataElement::Read(&search_pattern, params);
  if ((read_size == 0) ||
      (search_pattern.type() != DataElement::Type::kSequence)) {
    bt_log(SPEW, "sdp", "Failed to read search pattern");
    return;
  }
  size_t min_size = read_size + sizeof(uint16_t) + sizeof(uint8_t);
  if (params.size() < min_size) {
    bt_log(SPEW, "sdp", "Params too small: %d < %d", params.size(), min_size);
    return;
  }
  const DataElement* it;
  size_t count;
  for (count = 0, it = search_pattern.At(count); it != nullptr;
       it = search_pattern.At(++count)) {
    if ((count >= kMaxServiceSearchSize) ||
        (it->type() != DataElement::Type::kUuid)) {
      bt_log(SPEW, "sdp", "Search pattern invalid");
      service_search_pattern_.clear();
      return;
    }
    service_search_pattern_.emplace(*(it->Get<common::UUID>()));
  }
  if (count == 0) {
    bt_log(SPEW, "sdp", "Search pattern invalid: no records");
    return;
  }
  max_service_record_count_ = betoh16(params.view(read_size).As<uint16_t>());
  read_size += sizeof(uint16_t);
  if (!ParseContinuationState(params.view(read_size))) {
    service_search_pattern_.clear();
    return;
  }
  ZX_DEBUG_ASSERT(valid());
}

bool ServiceSearchRequest::valid() const {
  return service_search_pattern_.size() > 0 &&
         service_search_pattern_.size() <= kMaxServiceSearchSize &&
         max_service_record_count_ > 0;
}

common::ByteBufferPtr ServiceSearchRequest::GetPDU(TransactionId tid) const {
  if (!valid()) {
    return nullptr;
  }
  size_t size = sizeof(uint16_t) + sizeof(uint8_t) + cont_info_size();

  std::vector<DataElement> pattern(service_search_pattern_.size());
  size_t i = 0;
  for (auto& it : service_search_pattern_) {
    pattern.at(i).Set(it);
    i++;
  }
  DataElement search_pattern(std::move(pattern));

  size += search_pattern.WriteSize();
  auto buf = GetNewPDU(kServiceSearchRequest, tid, size);
  size_t written = sizeof(Header);

  // Write ServiceSearchPattern
  auto write_view = buf->mutable_view(written);
  written += search_pattern.Write(&write_view);
  // Write MaxServiceRecordCount
  uint16_t le = htobe16(max_service_record_count_);
  buf->Write(reinterpret_cast<uint8_t*>(&le), sizeof(uint16_t), written);
  written += sizeof(uint16_t);
  // Write Continuation State
  write_view = buf->mutable_view(written);
  written += WriteContinuationState(&write_view);

  ZX_DEBUG_ASSERT(written == sizeof(Header) + size);
  return buf;
}

ServiceSearchResponse::ServiceSearchResponse()
    : total_service_record_count_(0) {}

bool ServiceSearchResponse::complete() const {
  return total_service_record_count_ == service_record_handle_list_.size();
}

const common::BufferView ServiceSearchResponse::ContinuationState() const {
  if (!continuation_state_) {
    return common::BufferView();
  }
  return continuation_state_->view();
}

Status ServiceSearchResponse::Parse(const common::ByteBuffer& buf) {
  if (complete() && total_service_record_count_ != 0) {
    // This response was previously complete and non-empty.
    bt_log(SPEW, "sdp", "Can't parse into a complete response");
    return Status(HostError::kNotReady);
  }
  if (buf.size() < (2 * sizeof(uint16_t))) {
    bt_log(SPEW, "sdp", "Packet too small to parse");
    return Status(HostError::kPacketMalformed);
  }

  uint16_t total_service_record_count = betoh16(buf.As<uint16_t>());
  size_t read_size = sizeof(uint16_t);
  if (total_service_record_count_ != 0 &&
      total_service_record_count_ != total_service_record_count) {
    bt_log(SPEW, "sdp", "Continuing packet has different record count");
    return Status(HostError::kPacketMalformed);
  }
  total_service_record_count_ = total_service_record_count;

  uint16_t record_count = betoh16(buf.view(read_size).As<uint16_t>());
  read_size += sizeof(uint16_t);
  if ((buf.size() - read_size - sizeof(uint8_t)) <
      (sizeof(ServiceHandle) * record_count)) {
    bt_log(SPEW, "sdp", "Packet too small for %d records", record_count);
    return Status(HostError::kPacketMalformed);
  }
  for (uint16_t i = 0; i < record_count; i++) {
    auto view = buf.view(read_size + i * sizeof(ServiceHandle));
    service_record_handle_list_.emplace_back(betoh32(view.As<uint32_t>()));
  }
  read_size += sizeof(ServiceHandle) * record_count;
  common::BufferView cont_state_view;
  if (!ValidContinuationState(buf.view(read_size), &cont_state_view)) {
    bt_log(SPEW, "sdp", "Failed to find continuation state");
    return Status(HostError::kPacketMalformed);
  }
  if (cont_state_view.size() == 0) {
    continuation_state_ = nullptr;
  } else {
    continuation_state_ =
        std::make_unique<common::DynamicByteBuffer>(cont_state_view);
  }
  return Status();
}

// Continuation state: Index of the start record for the continued response.
common::MutableByteBufferPtr ServiceSearchResponse::GetPDU(
    uint16_t max, TransactionId tid,
    const common::ByteBuffer& cont_state) const {
  if (!complete()) {
    return nullptr;
  }
  // We never generate continuation for ServiceSearchResponses.
  // TODO(jamuraa): do we need to be concerned with MTU?
  if (cont_state.size() > 0) {
    return nullptr;
  }

  uint16_t response_record_count = total_service_record_count_;
  if (max < response_record_count) {
    bt_log(SPEW, "sdp", "Limit ServiceSearchResponse to %d records", max);
    response_record_count = max;
  }

  size_t size = (2 * sizeof(uint16_t)) +
                (response_record_count * sizeof(ServiceHandle)) +
                sizeof(uint8_t);

  auto buf = GetNewPDU(kServiceSearchResponse, tid, size);
  if (!buf) {
    return buf;
  }

  size_t written = sizeof(Header);
  // The total service record count and current service record count is the
  // same.
  uint16_t record_count_le = htobe16(response_record_count);
  buf->Write(reinterpret_cast<uint8_t*>(&record_count_le), sizeof(uint16_t),
             written);
  written += sizeof(uint16_t);
  buf->Write(reinterpret_cast<uint8_t*>(&record_count_le), sizeof(uint16_t),
             written);
  written += sizeof(uint16_t);

  for (size_t i = 0; i < response_record_count; i++) {
    uint32_t handle_le = htobe32(service_record_handle_list_.at(i));
    buf->Write(reinterpret_cast<uint8_t*>(&handle_le), sizeof(ServiceHandle),
               written);
    written += sizeof(ServiceHandle);
  }
  // There's no continuation state. Write the InfoLength.
  uint8_t info_length = 0;
  buf->Write(&info_length, sizeof(uint8_t), written);
  written += sizeof(uint8_t);
  ZX_DEBUG_ASSERT(written == sizeof(Header) + size);
  return buf;
}

}  // namespace sdp
}  // namespace btlib
