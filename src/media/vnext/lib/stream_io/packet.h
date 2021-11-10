// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_IO_PACKET_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_IO_PACKET_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/media/vnext/lib/stream_io/buffer_collection.h"
#include "src/media/vnext/lib/stream_io/payload_buffer.h"
#include "src/media/vnext/lib/stream_sink/converters.h"
#include "src/media/vnext/lib/stream_sink/release_fence.h"

namespace fmlib {

class Packet {
 public:
  Packet(PayloadBuffer payload_buffer, fuchsia::media2::PacketTimestamp timestamp,
         fuchsia::media2::PacketCompressionPropertiesPtr compression_properties = nullptr,
         fuchsia::media2::PacketEncryptionPropertiesPtr encryption_properties = nullptr,
         std::unique_ptr<ReleaseFence> release_fence = nullptr)
      : payload_buffer_(std::move(payload_buffer)),
        timestamp_(std::move(timestamp)),
        compression_properties_(std::move(compression_properties)),
        encryption_properties_(std::move(encryption_properties)),
        release_fence_(std::move(release_fence)) {
    FX_CHECK(payload_buffer_);
  }

  Packet(PayloadBuffer payload_buffer, int64_t timestamp,
         fuchsia::media2::PacketCompressionPropertiesPtr compression_properties = nullptr,
         fuchsia::media2::PacketEncryptionPropertiesPtr encryption_properties = nullptr,
         std::unique_ptr<ReleaseFence> release_fence = nullptr)
      : payload_buffer_(std::move(payload_buffer)),
        timestamp_(fuchsia::media2::PacketTimestamp::WithSpecified(std::move(timestamp))),
        compression_properties_(std::move(compression_properties)),
        encryption_properties_(std::move(encryption_properties)),
        release_fence_(std::move(release_fence)) {
    FX_CHECK(payload_buffer_);
  }

  ~Packet() = default;

  // TODO(dalesat): Represent multiple payload ranges.
  const fuchsia::media2::PayloadRange& payload_range() const {
    return payload_buffer_.payload_range();
  }

  void* data() const { return payload_buffer_.data(); }

  size_t size() const { return payload_buffer_.payload_range().size; }

  const fuchsia::media2::PacketTimestamp& timestamp() const { return timestamp_; }

  // TODO(dalesat): capture timestamp

  const fuchsia::media2::PacketCompressionPropertiesPtr& compression_properties() const {
    return compression_properties_;
  }

  const fuchsia::media2::PacketEncryptionPropertiesPtr& encryption_properties() const {
    return encryption_properties_;
  }

 private:
  PayloadBuffer payload_buffer_;
  fuchsia::media2::PacketTimestamp timestamp_;
  fuchsia::media2::PacketCompressionPropertiesPtr compression_properties_;
  fuchsia::media2::PacketEncryptionPropertiesPtr encryption_properties_;
  std::unique_ptr<ReleaseFence> release_fence_;
};

template <>
struct ToPacketConverter<std::unique_ptr<fmlib::Packet>> {
  static fuchsia::media2::Packet Convert(const std::unique_ptr<fmlib::Packet>& t) {
    FX_CHECK(t);
    return fuchsia::media2::Packet{
        .payload = {t->payload_range()},
        .timestamp = fidl::Clone(t->timestamp()),
        .compression_properties = fidl::Clone(t->compression_properties()),
        .encryption_properties = fidl::Clone(t->encryption_properties())};
  }
};

template <>
struct FromPacketConverter<std::unique_ptr<fmlib::Packet>, fmlib::InputBufferCollection*> {
  static std::unique_ptr<fmlib::Packet> Convert(fuchsia::media2::Packet packet,
                                                std::unique_ptr<ReleaseFence> release_fence,
                                                fmlib::InputBufferCollection* buffer_collection) {
    FX_CHECK(packet.payload.size() == 1);
    FX_CHECK(packet.timestamp.is_specified());
    FX_CHECK(release_fence);

    fmlib::PayloadBuffer payload_buffer =
        buffer_collection ? buffer_collection->GetPayloadBuffer(packet.payload[0])
                          : PayloadBuffer(packet.payload[0]);
    if (!payload_buffer) {
      return nullptr;
    }

    return std::make_unique<fmlib::Packet>(std::move(payload_buffer), fidl::Clone(packet.timestamp),
                                           fidl::Clone(packet.compression_properties),
                                           fidl::Clone(packet.encryption_properties),
                                           std::move(release_fence));
  }
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_IO_PACKET_H_
