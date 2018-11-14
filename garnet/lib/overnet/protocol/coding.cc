// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/overnet/protocol/coding.h"

namespace overnet {

namespace {

Border NoBorder(size_t size) { return Border::None(); }

StatusOr<Slice> BadCoding(Slice slice) {
  return StatusOr<Slice>(StatusCode::INVALID_ARGUMENT, "Unsupported codec");
}

StatusOr<Slice> IdCoding(Slice slice) { return slice; }

const CodecVTable kUnsupportedCodec = {"Unknown", NoBorder, BadCoding,
                                       BadCoding};
const CodecVTable kNilCodec = {"Identity", NoBorder, IdCoding, IdCoding};

}  // namespace

const CodecVTable* const kCodecVtable[256] = {
    &kNilCodec,         &kUnsupportedCodec, &kUnsupportedCodec,
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