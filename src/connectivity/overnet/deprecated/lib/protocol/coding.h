// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/deprecated/lib/vocabulary/slice.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/status.h"

// Utilities to encode/decode slices via some codec.
//
// The general encoded format is:
// (codec type : u8) (encoded bytes)

namespace overnet {

// Collection of functions that define a single codec.
struct CodecVTable {
  const char* const name;
  Border (*border_for_source_size)(size_t size);
  StatusOr<Slice> (*encode)(Slice slice);
  StatusOr<Slice> (*decode)(Slice slice);
};

// Mapping from codec identifying byte to the codec implementation.
extern const CodecVTable* const kCodecVtable[256];

// Currently named codecs. Future implementations may expand this.
enum class Coding : uint8_t {
  Identity = 0,
  Snappy = 1,
};

// Given a coding and a size, how much border should be allocated for a message?
inline Border BorderForSourceSize(Coding coding, size_t size) {
  return kCodecVtable[static_cast<uint8_t>(coding)]->border_for_source_size(size).WithAddedPrefix(
      1);
}

// Given a coding enum, get a name for the codec (or 'Unknown')
inline const char* CodingName(Coding coding) {
  return kCodecVtable[static_cast<uint8_t>(coding)]->name;
}

class SliceCodingOracle {
 public:
  SliceCodingOracle& SetSize(size_t size) {
    size_ = size;
    return *this;
  }

  Coding SuggestCoding();

 private:
  Optional<size_t> size_;
};

// Encode some data with a pre-selected coding.
StatusOr<Slice> Encode(Coding coding, Slice slice);
// Decode an encoded slice.
StatusOr<Slice> Decode(Slice slice);

// Encode some data with an auto-selected coding.
inline StatusOr<Slice> Encode(Slice slice) {
  return Encode(SliceCodingOracle().SetSize(slice.length()).SuggestCoding(), slice);
}

}  // namespace overnet
