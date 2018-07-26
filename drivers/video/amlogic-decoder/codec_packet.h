// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_PACKET_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_PACKET_H_

#include <lib/fxl/macros.h>

#include <stdint.h>
#include <limits>

class CodecBuffer;

// Instances of this class are 1:1 with fuchsia::mediacodec::CodecPacket.
class CodecPacket {
 public:
  ~CodecPacket();

  uint64_t buffer_lifetime_ordinal() const;

  uint32_t packet_index() const;

  const CodecBuffer& buffer() const;

  void SetStartOffset(uint32_t start_offset);
  bool has_start_offset() const;
  uint32_t start_offset() const;

  void SetValidLengthBytes(uint32_t valid_length_bytes);
  bool has_valid_length_bytes() const;
  uint32_t valid_length_bytes() const;

  void SetTimstampIsh(uint64_t timestamp_ish);
  bool has_timestamp_ish() const;
  uint64_t timestamp_ish() const;

  void SetFree(bool is_free);
  bool is_free() const;

 private:
  // The public section is for the core codec to call - the private section is
  // only for CodecImpl to call.
  friend class CodecImpl;

  static constexpr uint32_t kStartOffsetNotSet =
      std::numeric_limits<uint32_t>::max();
  static constexpr uint32_t kValidLengthBytesNotSet =
      std::numeric_limits<uint32_t>::max();
  static constexpr uint64_t kTimestampIshNotSet =
      std::numeric_limits<uint64_t>::max();

  // The buffer ptr is not owned.  The buffer lifetime is slightly longer than
  // the Packet lifetime.
  CodecPacket(uint64_t buffer_lifetime_ordinal, uint32_t packet_index,
              CodecBuffer* buffer);

  void ClearStartOffset();
  void ClearValidLengthBytes();
  void ClearTimestampIsh();

  uint64_t buffer_lifetime_ordinal_ = 0;
  uint32_t packet_index_ = 0;

  // not owned
  CodecBuffer* buffer_ = nullptr;

  uint32_t start_offset_ = kStartOffsetNotSet;
  uint32_t valid_length_bytes_ = kValidLengthBytesNotSet;
  uint64_t timestamp_ish_ = kTimestampIshNotSet;

  // is_free_
  //
  // This is tracked by the Codec server, not provided by the Codec client.
  //
  // True means free at protocol level.  False means in-flight at protocol
  // level.  This is used to check for nonsense from the client.
  //
  // When CodecPacket doesn't exist, that corresponds to packet not allocated at
  // the protocol level.
  //
  // An input packet starts out free with the client, and and output packet
  // starts out free with the codec server.  Either way, it starts free.
  bool is_free_ = true;

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CodecPacket);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_PACKET_H_
