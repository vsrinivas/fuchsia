// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/kcounter_inspect/vmo_file_with_update.h"

VmoFileWithUpdate::VmoFileWithUpdate(zx::vmo vmo, size_t offset, size_t length,
                                     fuchsia::kernel::CounterSyncPtr* kcounter)
    : offset_(offset), length_(length), vmo_(std::move(vmo)), kcounter_(kcounter) {}

VmoFileWithUpdate::~VmoFileWithUpdate() = default;

zx_status_t VmoFileWithUpdate::ReadAt(uint64_t length, uint64_t offset,
                                      std::vector<uint8_t>* out_data) {
  if (length == 0u || offset >= length_) {
    return ZX_OK;
  }

  zx_status_t status = Update();
  if (status != ZX_OK) {
    return status;
  }

  size_t remaining_length = length_ - offset;
  if (length > remaining_length) {
    length = remaining_length;
  }

  out_data->resize(length);
  return vmo_.read(out_data->data(), offset_ + offset, length);
}

void VmoFileWithUpdate::Describe(fuchsia::io::NodeInfo* out_info) {
  if (Update() != ZX_OK) {
    return;
  }
  zx::vmo temp_vmo;
  if (vmo_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ, &temp_vmo) != ZX_OK) {
    return;
  }
  out_info->vmofile() =
      fuchsia::io::Vmofile{.vmo = std::move(temp_vmo), .length = length_, .offset = offset_};
}

zx_status_t VmoFileWithUpdate::GetAttr(fuchsia::io::NodeAttributes* out_attributes) const {
  out_attributes->mode = fuchsia::io::MODE_TYPE_FILE | fuchsia::io::OPEN_RIGHT_READABLE;
  out_attributes->id = fuchsia::io::INO_UNKNOWN;
  out_attributes->content_size = length_;
  out_attributes->storage_size = length_;
  out_attributes->link_count = 1;
  out_attributes->creation_time = 0;
  out_attributes->modification_time = 0;
  return ZX_OK;
}

zx_status_t VmoFileWithUpdate::Update() {
  zx_status_t status;
  zx_status_t fidl_status = (*kcounter_)->UpdateInspectVMO(&status);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}
