// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-resources.h"

#include <utility>

namespace {

template <typename T>
bool CopyResources(size_t in_count, const T* in_list, fbl::Array<T>* out) {
  if (!in_count) {
    return true;
  }
  fbl::AllocChecker ac;
  out->reset(new (&ac) T[in_count], in_count);
  if (!ac.check()) {
    return false;
  }
  memcpy(out->begin(), in_list, in_count * sizeof(T));
  return true;
}

}  // namespace

namespace platform_bus {

zx_status_t DeviceResources::Init(const pbus_dev_t* pdev) {
  if (!CopyResources(pdev->mmio_count, pdev->mmio_list, &mmios_) ||
      !CopyResources(pdev->irq_count, pdev->irq_list, &irqs_) ||
      !CopyResources(pdev->bti_count, pdev->bti_list, &btis_) ||
      !CopyResources(pdev->smc_count, pdev->smc_list, &smcs_) ||
      !CopyResources(pdev->metadata_count, pdev->metadata_list, &metadata_) ||
      !CopyResources(pdev->boot_metadata_count, pdev->boot_metadata_list, &boot_metadata_)) {
    return ZX_ERR_NO_MEMORY;
  }

  if (!CopyMetadataDataBuffers()) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

bool DeviceResources::CopyMetadataDataBuffers() {
  // Each pbus_metadata_t struct contains a data_buffer pointer. We need to copy the
  // buffer into our own array and overwrite the existing pointer.
  fbl::AllocChecker ac;
  metadata_data_buffers_.reset(new (&ac) fbl::Array<uint8_t>[metadata_count()], metadata_count());
  if (!ac.check()) {
    return false;
  }
  for (size_t i = 0; i < metadata_count(); i++) {
    size_t data_len = metadata(i).data_size;
    auto data_buffer = static_cast<const uint8_t*>(metadata(i).data_buffer);
    if (!CopyResources(data_len, data_buffer, &metadata_data_buffers_[i])) {
      return false;
    }
    metadata_[i].data_buffer = metadata_data_buffers_[i].get();
  }
  return true;
}

}  // namespace platform_bus
