// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/cpp/wire/coding_errors.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire_format_metadata.h>

#include <cstring>
#include <tuple>

namespace fidl::internal {

std::vector<uint8_t> ConcatMetadataAndMessage(fidl::WireFormatMetadata metadata,
                                              fidl::OutgoingMessage& message) {
  std::vector<uint8_t> result;
  result.reserve(sizeof(fidl_opaque_wire_format_metadata_t) + message.CountBytes());
  fidl_opaque_wire_format_metadata_t opaque = metadata.ToOpaque();
  std::array<uint8_t, 8> metadata_bytes;
  static_assert(sizeof(opaque.metadata) == sizeof(metadata_bytes));
  memcpy(metadata_bytes.data(), &opaque.metadata, sizeof(opaque.metadata));
  // Copy the wire format metadata.
  result.insert(result.end(), metadata_bytes.begin(), metadata_bytes.end());
  // Copy the encoded bytes.
  auto bytes = message.CopyBytes();
  result.insert(result.end(), bytes.data(), bytes.data() + bytes.size());
  return result;
}

fit::result<fidl::Error, std::tuple<fidl::WireFormatMetadata, std::vector<uint8_t>>>
SplitMetadataAndMessage(cpp20::span<const uint8_t> persisted) {
  if (persisted.size() < sizeof(fidl_opaque_wire_format_metadata_t)) {
    return fit::error(fidl::Status::DecodeError(ZX_ERR_BUFFER_TOO_SMALL, kCodingErrorDataTooShort));
  }
  cpp20::span<const uint8_t> metadata_span =
      persisted.subspan(0, sizeof(fidl_opaque_wire_format_metadata_t));
  cpp20::span<const uint8_t> payload_span =
      persisted.subspan(sizeof(fidl_opaque_wire_format_metadata_t));
  fidl_opaque_wire_format_metadata opaque;
  memcpy(&opaque.metadata, metadata_span.data(), metadata_span.size());
  return fit::ok(std::make_tuple(fidl::WireFormatMetadata::FromOpaque(opaque),
                                 std::vector(payload_span.begin(), payload_span.end())));
}

}  // namespace fidl::internal
