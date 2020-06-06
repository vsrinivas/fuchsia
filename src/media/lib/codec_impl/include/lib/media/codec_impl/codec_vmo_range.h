// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_VMO_RANGE_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_VMO_RANGE_H_

#include <lib/zx/vmo.h>

#include <variant>

// This is a move only helper class around a slice of a VMO.
//
// Currently, it works for both owned and unowned VMOs, but the use case for the latter is limited
// to CodecFrames. Typically, CodecBuffer owns the CodecVmoRange, but when a CodecBuffer is used to
// initialize a CodecFrame, the CodecFrame wants to make a copy of the CodecBuffer's CodecVmoRange.
// Rather than duplicating the vmo, the CodecFrame will borrow the CodecBuffer's vmo.
class CodecVmoRange {
 public:
  CodecVmoRange(zx::vmo vmo, uint64_t offset, size_t size);
  CodecVmoRange(zx::unowned_vmo, uint64_t offset, size_t size);

  // Move only class
  CodecVmoRange(CodecVmoRange&&) = default;
  CodecVmoRange& operator=(CodecVmoRange&&) = default;

  CodecVmoRange(const CodecVmoRange&) = delete;
  CodecVmoRange& operator=(const CodecVmoRange&) = delete;

  const zx::vmo& vmo() const;
  uint64_t offset() const { return offset_; }
  size_t size() const { return size_; }

 private:
  using VmoVariant = std::variant<zx::vmo, zx::unowned_vmo>;

  VmoVariant vmo_v_;
  uint64_t offset_;
  size_t size_;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_VMO_RANGE_H_
