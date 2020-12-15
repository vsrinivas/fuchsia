// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/vmo_metadata_buffer.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <memory>

namespace fvm {

namespace {

constexpr const char kVmoName[] = "fvm-metadata";

}  // namespace

VmoMetadataBuffer::VmoMetadataBuffer(fzl::OwnedVmoMapper vmo) : vmo_(std::move(vmo)) {}

VmoMetadataBuffer::~VmoMetadataBuffer() = default;

std::unique_ptr<MetadataBuffer> VmoMetadataBuffer::Create(size_t size) const {
  fzl::OwnedVmoMapper vmo;
  ZX_ASSERT(vmo.CreateAndMap(size, kVmoName) == ZX_OK);
  return std::make_unique<VmoMetadataBuffer>(std::move(vmo));
}

}  // namespace fvm
