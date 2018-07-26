// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_input_item.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fxl/logging.h>

CodecInputItem::CodecInputItem() : is_valid_(false) {
  // nothing else to do here
}

CodecInputItem::CodecInputItem(
    const fuchsia::mediacodec::CodecFormatDetails& format_details)
    : format_details_(std::make_unique<fuchsia::mediacodec::CodecFormatDetails>(
          fidl::Clone(format_details))) {
  // nothing else to do here
}

CodecInputItem::CodecInputItem(const CodecPacket* packet) : packet_(packet) {
  // nothing else do to here
}

CodecInputItem CodecInputItem::Invalid() { return CodecInputItem(); }

// Don't std::move() the caller's format_details, for now.
CodecInputItem CodecInputItem::FormatDetails(
    const fuchsia::mediacodec::CodecFormatDetails& format_details) {
  return CodecInputItem(format_details);
}

CodecInputItem CodecInputItem::Packet(const CodecPacket* packet) {
  FXL_DCHECK(packet);
  return CodecInputItem(packet);
}

CodecInputItem CodecInputItem::EndOfStream() { return CodecInputItem(nullptr); }

bool CodecInputItem::is_valid() const { return is_valid_; }

bool CodecInputItem::is_format_details() const {
  return is_valid() && !!format_details_;
}

bool CodecInputItem::is_packet() const { return is_valid() && !!packet_; }

bool CodecInputItem::is_end_of_stream() const {
  return is_valid() && !format_details_ && !packet_;
}

const fuchsia::mediacodec::CodecFormatDetails&
CodecInputItem::format_details() {
  FXL_DCHECK(is_format_details());
  return *format_details_;
}

const CodecPacket* CodecInputItem::packet() const {
  FXL_DCHECK(is_packet());
  return packet_;
}
