// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_INPUT_ITEM_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_INPUT_ITEM_H_

#include <lib/fxl/macros.h>

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <memory>

class CodecPacket;

class CodecInputItem {
 public:
  // Move-only.
  //
  // Defaulting either (or both) of these auto-deletes implicit copy and
  // implicit assign.  In other words, defaulting counts as user-declared.
  CodecInputItem(CodecInputItem&& from) = default;
  CodecInputItem& operator=(CodecInputItem&& from) = default;

  static CodecInputItem Invalid();
  static CodecInputItem FormatDetails(
      const fuchsia::mediacodec::CodecFormatDetails& format_details);
  static CodecInputItem Packet(const CodecPacket* packet);
  static CodecInputItem EndOfStream();

  bool is_valid() const;
  bool is_format_details() const;
  bool is_packet() const;
  bool is_end_of_stream() const;

  const fuchsia::mediacodec::CodecFormatDetails& format_details();
  const CodecPacket* packet() const;

 private:
  // !is_valid()
  CodecInputItem();
  explicit CodecInputItem(
      const fuchsia::mediacodec::CodecFormatDetails& format_details);
  explicit CodecInputItem(const CodecPacket* packet);

  // The fields of this class are relied upon to be move-able.

  bool is_valid_ = true;
  std::unique_ptr<fuchsia::mediacodec::CodecFormatDetails> format_details_;
  // If nullptr, is_end_of_stream() is true.
  const CodecPacket* packet_ = nullptr;

  // Lack of format_details_ and lack of packet_ means is_end_of_stream().
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_INPUT_ITEM_H_
