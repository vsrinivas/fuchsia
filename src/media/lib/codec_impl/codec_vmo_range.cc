// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/codec_impl/codec_vmo_range.h>

#include <variant>

CodecVmoRange::CodecVmoRange(zx::vmo vmo, uint64_t offset, size_t size)
    : vmo_v_(std::move(vmo)), offset_(offset), size_(size) {}

CodecVmoRange::CodecVmoRange(zx::unowned_vmo vmo, uint64_t offset, size_t size)
    : vmo_v_(std::move(vmo)), offset_(offset), size_(size) {}

const zx::vmo& CodecVmoRange::vmo() const {
  return std::holds_alternative<zx::vmo>(vmo_v_) ? std::get<zx::vmo>(vmo_v_)
                                                 : *std::get<zx::unowned_vmo>(vmo_v_);
}
