// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_FORMAT_METADATA_H_
#define LIB_FIDL_CPP_WIRE_FORMAT_METADATA_H_

#include <zircon/fidl.h>

#include <cstdbool>
#include <cstdint>
#include <cstring>

namespace fidl {

class WireFormatMetadata;

namespace internal {

enum class WireFormatVersion {
  // V1 wire format: features extensible unions (xunions).
  // Starting at 1 to invalidate a default constructed |WireFormatVersion|.
  kV1 = FIDL_WIRE_FORMAT_VERSION_V1,

  // V2 wire format: features efficient envelopes and inlining small values in
  // envelopes.
  kV2 = FIDL_WIRE_FORMAT_VERSION_V2,
};

// Constructs a |WireFormatMetadata| corresponding to the version.
WireFormatMetadata WireFormatMetadataForVersion(WireFormatVersion version);

}  // namespace internal

// Wire format metadata describing the format and revision of an encoded FIDL
// message. This class is shared by the various C++ FIDL bindings.
class WireFormatMetadata {
 public:
  // Creates a |WireFormatMetadata| from an opaque binary representation.
  static WireFormatMetadata FromOpaque(fidl_opaque_wire_format_metadata_t opaque);

  // Creates a |WireFormatMetadata| by extracting the relevant information from
  // a transactional header.
  static WireFormatMetadata FromTransactionalHeader(fidl_message_header_t header);

  // Export this |WireFormatMetadata| to an opaque binary representation, which
  // may be later sent down the wire.
  fidl_opaque_wire_format_metadata_t ToOpaque() const;

  // Returns if the metadata is valid (e.g. recognized magic number).
  bool is_valid() const;

  // Returns the wire format version.
  //
  // Will panic if the metadata is invalid (e.g. unknown magic number). Callers
  // should first validate the metadata or the transactional header from which
  // it is derived.
  internal::WireFormatVersion wire_format_version() const;

  // Returns the wire format version as a C enum.
  //
  // Will panic if the metadata is invalid (e.g. unknown magic number). Callers
  // should first validate the metadata or the transactional header from which
  // it is derived.
  ::FidlWireFormatVersion c_wire_format_version() const;

 private:
  friend WireFormatMetadata internal::WireFormatMetadataForVersion(
      internal::WireFormatVersion version);

  WireFormatMetadata() = default;

  uint8_t disambiguator_ = 0;
  uint8_t magic_number_ = 0;
  uint8_t at_rest_flags_[2] = {};
  uint8_t reserved_[4] = {};
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_FORMAT_METADATA_H_
