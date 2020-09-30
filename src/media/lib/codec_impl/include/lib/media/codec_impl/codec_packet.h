// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_PACKET_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_PACKET_H_

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>

#include <limits>
#include <memory>

#include <fbl/macros.h>

class CodecBuffer;
class CodecPacketForTest;

// Instances of this class are 1:1 with fuchsia::media::Packet.
class CodecPacket {
 public:
  ~CodecPacket();

  uint64_t buffer_lifetime_ordinal() const;

  uint32_t packet_index() const;

  void SetBuffer(const CodecBuffer* buffer);
  const CodecBuffer* buffer() const;

  void SetStartOffset(uint32_t start_offset);
  bool has_start_offset() const;
  uint32_t start_offset() const;

  void SetValidLengthBytes(uint32_t valid_length_bytes);
  bool has_valid_length_bytes() const;
  uint32_t valid_length_bytes() const;

  void SetTimstampIsh(uint64_t timestamp_ish);
  // Sets timestamp_ish() to kTimestampIshNotSet, which also causes
  // has_timestamp_ish() to return false.
  void ClearTimestampIsh();
  bool has_timestamp_ish() const;
  uint64_t timestamp_ish() const;

  void SetFree(bool is_free);
  bool is_free() const;

  void SetIsNew(bool is_new);
  bool is_new() const;

  void SetKeyFrame(bool key_frame);
  void ClearKeyFrame();
  bool has_key_frame() const;
  bool key_frame() const;

  void CacheFlush() const;

 private:
  // The public section is for the core codec to call - the private section is
  // only for CodecImpl to call.
  friend class CodecImpl;
  friend class CodecPacketForTest;

  static constexpr uint32_t kStartOffsetNotSet = std::numeric_limits<uint32_t>::max();
  static constexpr uint32_t kValidLengthBytesNotSet = std::numeric_limits<uint32_t>::max();

  // The buffer ptr is not owned.  The buffer lifetime is slightly longer than
  // the Packet lifetime.
  CodecPacket(uint64_t buffer_lifetime_ordinal, uint32_t packet_index);

  void ClearStartOffset();
  void ClearValidLengthBytes();

  uint64_t buffer_lifetime_ordinal_ = 0;
  uint32_t packet_index_ = 0;

  // The buffer_ is meaningful only while a packet_index is in-flight, not
  // while the packet_index is free.
  const CodecBuffer* buffer_ = nullptr;

  uint32_t start_offset_ = kStartOffsetNotSet;
  uint32_t valid_length_bytes_ = kValidLengthBytesNotSet;

  // Allow all timestamp_ish values to be valid by carying valid bool
  // separately.
  bool has_timestamp_ish_ = false;
  uint64_t timestamp_ish_ = 0;

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

  // Starts true when a packet is truly new.  In addition, a CodecAdapter may
  // set this back to true whenever the packet is logically new from the
  // CodecAdapter's point of view.  This allows for the CodecAdapter to
  // determine whether to recycle a packet to the core codec depending on
  // whether the packet is new or not, on first call to
  // CoreCodecRecycleOutputPacket().  Some core codecs potentially want an
  // internal recycle call or equivalent for new packets, while others don't
  // (such as amlogic-video).
  bool is_new_ = true;

  // Set to true if this packet is part of a key frame.
  bool key_frame_ = false;
  bool key_frame_is_set_ = false;

  CodecPacket() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecPacket);
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_PACKET_H_
