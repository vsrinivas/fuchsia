// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stop_reply_packet.h"

#include "lib/ftl/logging.h"

#include "util.h"

namespace debugserver {
namespace {

const char kThreadIdPrefix[] = "thread:";

template <class T>
void InsertChars(std::vector<char>& collection, const T& to_insert) {
  collection.insert(collection.end(), to_insert.begin(), to_insert.end());
}

void InsertString(std::vector<char>& collection, const std::string& to_insert) {
  InsertChars<std::string>(collection, to_insert);
}

void InsertVector(std::vector<char>& collection,
                  const std::vector<char>& to_insert) {
  InsertChars<std::vector<char>>(collection, to_insert);
}

}  // namespace

StopReplyPacket::StopReplyPacket(Type type) : type_(type), signo_(0) {}

void StopReplyPacket::SetSignalNumber(uint8_t signal_number) {
  FTL_DCHECK(type_ == Type::kReceivedSignal);
  signo_ = signal_number;
}

void StopReplyPacket::SetThreadId(mx_koid_t process_id, mx_koid_t thread_id) {
  FTL_DCHECK(type_ == Type::kReceivedSignal);
  tid_string_ = util::EncodeThreadId(process_id, thread_id);
}

void StopReplyPacket::AddRegisterValue(uint8_t register_number,
                                       const ftl::StringView& value) {
  FTL_DCHECK(type_ == Type::kReceivedSignal);
  FTL_DCHECK(!value.empty());

  // Encode the register value here as it will appear in the packet:
  // XX:value
  std::vector<char> result;
  result.resize(3 + value.size());

  char* ptr = result.data();
  util::EncodeByteString(register_number, ptr);
  ptr += 2;
  *ptr++ = ':';
  std::memcpy(ptr, value.data(), value.size());

  register_values_.push_back(std::move(result));
}

void StopReplyPacket::SetStopReason(const ftl::StringView& reason) {
  FTL_DCHECK(type_ == Type::kReceivedSignal);
  stop_reason_ = reason.ToString() + ":";
}

std::vector<char> StopReplyPacket::Build() const {
  // We only support kReceivedSignal for now.
  FTL_DCHECK(type_ == Type::kReceivedSignal);
  FTL_DCHECK(signo_) << "A signal number is required";

  char type = HasParameters() ? 'T' : 'S';

  // Calculate the final packet size.
  std::vector<char> packet;

  // Type
  packet.push_back(type);

  // Sigval
  uint8_t signo = stop_reason_.empty() ? signo_ : 5;
  char signo_str[2];
  util::EncodeByteString(signo, signo_str);
  packet.insert(packet.end(), signo_str, signo_str + 2);

  // Registers
  for (const auto& regval : register_values_) {
    InsertVector(packet, regval);
    packet.push_back(';');
  }

  // Thread ID.
  if (!tid_string_.empty()) {
    InsertString(packet, kThreadIdPrefix);
    InsertString(packet, tid_string_);
    packet.push_back(';');
  }

  // Stop reason
  if (!stop_reason_.empty()) {
    InsertString(packet, stop_reason_);
    packet.push_back(';');
  }

  return packet;
}

bool StopReplyPacket::HasParameters() const {
  FTL_DCHECK(type_ == Type::kReceivedSignal);
  return !tid_string_.empty() || !register_values_.empty() ||
         !stop_reason_.empty();
}

}  // namespace debugserver
