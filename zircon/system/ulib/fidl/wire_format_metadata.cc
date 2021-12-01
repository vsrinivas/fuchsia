// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

#include <cstring>

namespace fidl {
namespace internal {

WireFormatMetadata WireFormatMetadata::FromOpaque(fidl_opaque_wire_format_metadata_t opaque) {
  uint8_t bytes[8] = {};
  memcpy(bytes, &opaque, sizeof(opaque));
  WireFormatMetadata metadata;
  metadata.disambiguator_ = bytes[0];
  metadata.magic_number_ = bytes[1];
  metadata.at_rest_flags_[0] = bytes[2];
  metadata.at_rest_flags_[1] = bytes[3];
  metadata.reserved_[0] = bytes[4];
  metadata.reserved_[1] = bytes[5];
  metadata.reserved_[2] = bytes[6];
  metadata.reserved_[3] = bytes[7];
  return metadata;
}

WireFormatMetadata WireFormatMetadata::FromTransactionalHeader(fidl_message_header_t header) {
  WireFormatMetadata metadata;
  metadata.disambiguator_ = 0;
  metadata.magic_number_ = header.magic_number;
  // See
  // https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0138_handling_unknown_interactions?hl=en#transactional-message-header-v4
  //
  // At-rest flags come first, followed by dynamic flags.
  //
  // Note: the V1, V2, and "V2 after unknown interactions" FIDL wire formats all
  // store the at_rest flags in the same location. When a future FIDL revision
  // change the location of the at_rest flags, this would need to be updated to
  // be conditional on the magic number.
  metadata.at_rest_flags_[0] = header.flags[0];
  metadata.at_rest_flags_[1] = header.flags[1];
  memset(metadata.reserved_, 0, sizeof(metadata.reserved_));
  return metadata;
}

fidl_opaque_wire_format_metadata_t WireFormatMetadata::ToOpaque() const {
  // Translate the metadata to a binary representation.
  // We could use a for-loop or fancier serialization, but an array gives the
  // most explicit control over the ABI.
  uint8_t bytes[8] = {
      disambiguator_, magic_number_, at_rest_flags_[0], at_rest_flags_[1],
      reserved_[0],   reserved_[1],  reserved_[2],      reserved_[3],
  };
  uint64_t opaque;
  static_assert(sizeof(opaque) == sizeof(bytes), "Opaque metadata is 8 bytes");
  memcpy(&opaque, bytes, sizeof(opaque));
  return fidl_opaque_wire_format_metadata_t{opaque};
}

bool WireFormatMetadata::is_valid() const {
  // Note: this method should be in sync with |fidl_validate_txn_header|.
  // TODO(fxbug.dev/88366): Support the upcoming wire format magic in RFC-0138.
  return magic_number_ == kFidlWireFormatMagicNumberInitial;
}

WireFormatVersion WireFormatMetadata::wire_format_version() const {
  ZX_ASSERT_MSG(is_valid(), "Invalid metadata %d %d %d", at_rest_flags_[0], at_rest_flags_[1],
                magic_number_);
  if ((at_rest_flags_[0] & FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2) == 0) {
    return WireFormatVersion::kV1;
  }
  return WireFormatVersion::kV2;
}

::FidlWireFormatVersion WireFormatMetadata::c_wire_format_version() const {
  switch (wire_format_version()) {
    case WireFormatVersion::kV1:
      return FIDL_WIRE_FORMAT_VERSION_V1;
    case WireFormatVersion::kV2:
      return FIDL_WIRE_FORMAT_VERSION_V2;
  }
  ZX_PANIC("Unsupported wire format version %d", static_cast<int>(wire_format_version()));
}

}  // namespace internal
}  // namespace fidl
