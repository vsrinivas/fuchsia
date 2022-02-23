// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_encoder.h>
#include <lib/fidl/txn_header.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

namespace fidl::internal {
namespace {

const size_t kSmallAllocSize = 512;
const size_t kLargeAllocSize = ZX_CHANNEL_MAX_MSG_BYTES;

size_t Align(size_t size) {
  constexpr size_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (size + alignment_mask) & ~alignment_mask;
}

}  // namespace

NaturalEncoder::NaturalEncoder(zx_handle_disposition_t* handles, uint32_t handle_capacity)
    : handles_(handles), handle_capacity_(handle_capacity) {}
NaturalEncoder::NaturalEncoder(zx_handle_disposition_t* handles, uint32_t handle_capacity,
                               internal::WireFormatVersion wire_format)
    : handles_(handles), handle_capacity_(handle_capacity), wire_format_(wire_format) {}

size_t NaturalEncoder::Alloc(size_t size) {
  size_t offset = bytes_.size();
  size_t new_size = bytes_.size() + Align(size);

  if (likely(new_size <= kSmallAllocSize)) {
    bytes_.reserve(kSmallAllocSize);
  } else if (likely(new_size <= kLargeAllocSize)) {
    bytes_.reserve(kLargeAllocSize);
  } else {
    bytes_.reserve(new_size);
  }
  bytes_.resize(new_size);

  return offset;
}

#ifdef __Fuchsia__
void NaturalEncoder::EncodeHandle(zx::object_base* value, zx_obj_type_t obj_type,
                                  zx_rights_t rights, size_t offset) {
  if (value->is_valid()) {
    *GetPtr<zx_handle_t>(offset) = FIDL_HANDLE_PRESENT;
    ZX_ASSERT(handle_actual_ <= handle_capacity_);
    handles_[handle_actual_] = zx_handle_disposition_t{
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = value->release(),
        .type = obj_type,
        .rights = rights,
        .result = ZX_OK,
    };
    handle_actual_++;
  } else {
    *GetPtr<zx_handle_t>(offset) = FIDL_HANDLE_ABSENT;
  }
}
#endif

HLCPPOutgoingBody NaturalBodyEncoder::GetBody() {
  return HLCPPOutgoingBody(BytePart(bytes_.data(), static_cast<uint32_t>(bytes_.size()),
                                    static_cast<uint32_t>(bytes_.size())),
                           HandleDispositionPart(handles_, static_cast<uint32_t>(handle_actual_),
                                                 static_cast<uint32_t>(handle_actual_)));
}

void NaturalBodyEncoder::Reset() {
  bytes_.clear();
  handle_actual_ = 0;
}

}  // namespace fidl::internal
