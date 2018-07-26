// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_packet.h"

#include "codec_buffer.h"

#include <lib/fxl/logging.h>

#include <stdint.h>

CodecPacket::CodecPacket(uint64_t buffer_lifetime_ordinal,
                         uint32_t packet_index, CodecBuffer* buffer)
    : buffer_lifetime_ordinal_(buffer_lifetime_ordinal),
      packet_index_(packet_index),
      buffer_(buffer) {
  // nothing else to do here
}

CodecPacket::~CodecPacket() {
  // nothing else to do here
}

uint64_t CodecPacket::buffer_lifetime_ordinal() const {
  return buffer_lifetime_ordinal_;
}

uint32_t CodecPacket::packet_index() const { return packet_index_; }

const CodecBuffer& CodecPacket::buffer() const { return *buffer_; }

void CodecPacket::SetStartOffset(uint32_t start_offset) {
  start_offset_ = start_offset;
}

bool CodecPacket::has_start_offset() const {
  return start_offset_ != kStartOffsetNotSet;
}

uint32_t CodecPacket::start_offset() const { return start_offset_; }

void CodecPacket::SetValidLengthBytes(uint32_t valid_length_bytes) {
  valid_length_bytes_ = valid_length_bytes;
}

bool CodecPacket::has_valid_length_bytes() const {
  return valid_length_bytes_ != kValidLengthBytesNotSet;
}

uint32_t CodecPacket::valid_length_bytes() const { return valid_length_bytes_; }

void CodecPacket::SetTimstampIsh(uint64_t timestamp_ish) {
  timestamp_ish_ = timestamp_ish;
}

bool CodecPacket::has_timestamp_ish() const {
  return timestamp_ish_ != kTimestampIshNotSet;
}

uint64_t CodecPacket::timestamp_ish() const { return timestamp_ish_; }

void CodecPacket::SetFree(bool is_free) {
  // We shouldn't need to be calling this method unless we're changing the
  // is_free state.
  FXL_DCHECK(is_free_ != is_free);
  is_free_ = is_free;
}

bool CodecPacket::is_free() const { return is_free_; }

void CodecPacket::ClearStartOffset() { start_offset_ = kStartOffsetNotSet; }

void CodecPacket::ClearValidLengthBytes() {
  valid_length_bytes_ = kValidLengthBytesNotSet;
}

void CodecPacket::ClearTimestampIsh() { timestamp_ish_ = kTimestampIshNotSet; }
