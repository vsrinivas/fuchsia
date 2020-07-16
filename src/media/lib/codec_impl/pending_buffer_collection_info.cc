// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/codec_impl/pending_buffer_collection_info.h>

#include "fuchsia/sysmem/cpp/fidl.h"

namespace codec_impl {
namespace internal {
namespace {

PendingBufferCollectionInfo::InfoResult GetInfoResult(
    zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 info) {
  if (status != ZX_OK) {
    return fit::error(status);
  } else {
    return fit::ok(std::move(info));
  }
}

}  // namespace

PendingBufferCollectionInfo::PendingBufferCollectionInfo(
    CodecPort port, uint64_t buffer_lifetime_ordinal,
    const std::optional<fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>&
        aux_buffer_constraints)
    : port_(port),
      buffer_lifetime_ordinal_(buffer_lifetime_ordinal),
      aux_buffer_requirement_(GetAuxBufferRequirement(aux_buffer_constraints)) {}

void PendingBufferCollectionInfo::set_buffer_collection_info(
    zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 info) {
  buffer_collection_ = GetInfoResult(status, std::move(info));
}

void PendingBufferCollectionInfo::set_aux_buffer_collection_info(
    zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 info) {
  ZX_DEBUG_ASSERT(AllowsAuxBuffersForSecure());
  aux_buffer_collection_ = GetInfoResult(status, std::move(info));
}

fuchsia::sysmem::BufferCollectionInfo_2 PendingBufferCollectionInfo::TakeBufferCollectionInfo() {
  return buffer_collection_.take_value();
}

std::optional<fuchsia::sysmem::BufferCollectionInfo_2>
PendingBufferCollectionInfo::TakeAuxBufferCollectionInfo() {
  std::optional<fuchsia::sysmem::BufferCollectionInfo_2> maybe_info;
  if (HasValidAuxBufferCollection()) {
    maybe_info = aux_buffer_collection_.take_value();
  }
  return maybe_info;
}

bool PendingBufferCollectionInfo::HasValidAuxBufferCollection() const {
  // Per GetAuxBuffers documentation, if vmo[0] is invalid, then sysmem determined that aux
  // buffers were not required by any participant. In that case, just leave the
  // aux_buffer_collection in the pending state as if we didn't need them.
  return aux_buffer_collection_.is_ok() && aux_buffer_collection_.value().buffers[0].vmo.is_valid();
}

// static
PendingBufferCollectionInfo::AuxBufferRequirement
PendingBufferCollectionInfo::GetAuxBufferRequirement(
    const std::optional<fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>&
        aux_buffer_constraints) {
  AuxBufferRequirement r = AuxBufferRequirement::DISALLOWED;
  if (aux_buffer_constraints.has_value()) {
    if (aux_buffer_constraints->need_clear_aux_buffers_for_secure) {
      r = AuxBufferRequirement::REQUIRED;
    } else if (aux_buffer_constraints->allow_clear_aux_buffers_for_secure) {
      r = AuxBufferRequirement::ALLOWED;
    }
  }
  return r;
}

}  // namespace internal
}  // namespace codec_impl
