// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <memory>

#include <fvm/metadata-buffer.h>

namespace fvm {

HeapMetadataBuffer::HeapMetadataBuffer(std::unique_ptr<uint8_t[]> buffer, size_t size)
    : buffer_(std::move(buffer)), size_(size) {}

HeapMetadataBuffer::~HeapMetadataBuffer() = default;

std::unique_ptr<MetadataBuffer> HeapMetadataBuffer::Create(size_t size) const {
  return std::make_unique<HeapMetadataBuffer>(std::unique_ptr<uint8_t[]>(new uint8_t[size]), size);
}

}  // namespace fvm
