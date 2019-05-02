// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/protocol/coding.h"
#include <snappy.h>

namespace overnet {

////////////////////////////////////////////////////////////////////////////////
// API implementation

Coding SliceCodingOracle::SuggestCoding() {
  // If we don't know the size, just guess maximum compression.
  if (!size_.has_value()) {
    return Coding::Snappy;
  }

  // For really small things avoid extra allocations.
  if (*size_ < 256) {
    return Coding::Identity;
  }

  return Coding::Snappy;
}

StatusOr<Slice> Encode(Coding coding, Slice slice) {
  if (slice.length() == 0) {
    return slice;
  }
  auto status =
      kCodecVtable[static_cast<uint8_t>(coding)]->encode(std::move(slice));
  if (status.is_error()) {
    return status;
  }
  assert(status->length() != 0);
  return status->WithPrefix(
      1, [coding](uint8_t* p) { *p = static_cast<uint8_t>(coding); });
}

StatusOr<Slice> Decode(Slice slice) {
  if (slice.length() == 0) {
    return slice;
  }
  Coding coding = static_cast<Coding>(*slice.begin());
  slice.TrimBegin(1);
  return kCodecVtable[static_cast<uint8_t>(coding)]->decode(std::move(slice));
}

////////////////////////////////////////////////////////////////////////////////
// Codec implementations go in this super secret namespace.

namespace {

////////////////////////////////////////////////////////////////////////////////
// Basic encoders and utilities

Border NoBorder(size_t size) { return Border::None(); }

StatusOr<Slice> BadCoding(Slice slice) {
  return StatusOr<Slice>(StatusCode::INVALID_ARGUMENT, "Unsupported codec");
}

StatusOr<Slice> IdCoding(Slice slice) { return slice; }

////////////////////////////////////////////////////////////////////////////////
// Snappy support

StatusOr<Slice> SnappyEncode(Slice slice) {
  size_t alloc_len = snappy::MaxCompressedLength(slice.length());
  size_t final_len;
  Slice out = Slice::WithInitializer(alloc_len, [&](uint8_t* buffer) {
    snappy::RawCompress(reinterpret_cast<const char*>(slice.begin()),
                        slice.length(), reinterpret_cast<char*>(buffer),
                        &final_len);
  });
  out.TrimEnd(alloc_len - final_len);
  return out;
}

StatusOr<Slice> SnappyDecode(Slice slice) {
  size_t uncompressed_length;
  if (!snappy::GetUncompressedLength(
          reinterpret_cast<const char*>(slice.begin()), slice.length(),
          &uncompressed_length)) {
    return Status(StatusCode::INVALID_ARGUMENT,
                  "Cannot determine uncompressed length from Snappy buffer");
  }
  // If expansion is large, verify that it's a valid buffer before trying to
  // uncompress.
  if (uncompressed_length > 1024 * 1024 ||
      uncompressed_length > 10 * slice.length()) {
    if (!snappy::IsValidCompressedBuffer(
            reinterpret_cast<const char*>(slice.begin()), slice.length())) {
      return Status(StatusCode::INVALID_ARGUMENT, "Invalid Snappy data");
    }
  }
  bool ok;
  Slice output =
      Slice::WithInitializer(uncompressed_length, [&](uint8_t* buffer) {
        ok = snappy::RawUncompress(reinterpret_cast<const char*>(slice.begin()),
                                   slice.length(),
                                   reinterpret_cast<char*>(buffer));
      });
  if (!ok) {
    return Status(StatusCode::INVALID_ARGUMENT,
                  "Failed to decompress Snappy data");
  }
  return output;
}

////////////////////////////////////////////////////////////////////////////////
// Codec tables

const CodecVTable kUnsupportedCodec = {"Unknown", NoBorder, BadCoding,
                                       BadCoding};
const CodecVTable kNilCodec = {"Identity", NoBorder, IdCoding, IdCoding};
const CodecVTable kSnappyCodec = {"Snappy", NoBorder, SnappyEncode,
                                  SnappyDecode};

}  // namespace

const CodecVTable* const kCodecVtable[256] = {
    &kNilCodec,         &kSnappyCodec,      &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec, &kUnsupportedCodec, &kUnsupportedCodec,
    &kUnsupportedCodec,
};

}  // namespace overnet
