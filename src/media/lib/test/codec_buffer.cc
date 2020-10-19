// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/test/codec_buffer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdio.h>

CodecBuffer::CodecBuffer(uint32_t buffer_index, size_t size_bytes)
    : buffer_index_(buffer_index), size_bytes_(size_bytes) {}

uint32_t CodecBuffer::buffer_index() const { return buffer_index_; }

CodecBuffer::~CodecBuffer() {
  if (base_) {
    zx_status_t res = zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(base_), size_bytes_);
    if (res != ZX_OK) {
      FX_PLOGS(FATAL, res) << "Failed to unmap " << size_bytes_ << " byte buffer vmo";
    }
    base_ = nullptr;
  }
}

bool CodecBuffer::GetDupVmo(bool is_for_write, zx::vmo* out_vmo) {
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP;
  if (is_for_write) {
    rights |= ZX_RIGHT_WRITE;
  }
  zx_status_t res = vmo_.duplicate(rights, out_vmo);
  if (res != ZX_OK) {
    printf("Failed to duplicate buffer vmo handle (res %d)\n", res);
    return false;
  }
  return true;
}

bool CodecBuffer::CreateFromVmoInternal(zx::vmo vmo, uint32_t vmo_usable_start,
                                        uint32_t vmo_usable_size, bool need_write,
                                        bool is_physically_contiguous) {
  ZX_DEBUG_ASSERT(vmo);
  ZX_DEBUG_ASSERT(vmo_usable_size != 0);
  zx_vm_option_t options = ZX_VM_PERM_READ;
  if (need_write) {
    options |= ZX_VM_PERM_WRITE;
  }
  uintptr_t tmp;
  zx_status_t status =
      zx::vmar::root_self()->map(options, 0, vmo, vmo_usable_start, vmo_usable_size, &tmp);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "CodecBuffer::CreateFromVmoInternal failed to map VMO";
    return false;
  }
  base_ = reinterpret_cast<uint8_t*>(tmp);
  vmo_ = std::move(vmo);
  is_physically_contiguous_ = is_physically_contiguous;
  return true;
}

std::unique_ptr<CodecBuffer> CodecBuffer::CreateFromVmo(uint32_t buffer_index, zx::vmo vmo,
                                                        uint32_t vmo_usable_start,
                                                        uint32_t vmo_usable_size, bool need_write,
                                                        bool is_physically_contiguous) {
  std::unique_ptr<CodecBuffer> result(new CodecBuffer(buffer_index, vmo_usable_size));
  if (!result->CreateFromVmoInternal(std::move(vmo), vmo_usable_start, vmo_usable_size, need_write,
                                     is_physically_contiguous)) {
    return nullptr;
  }
  return result;
}
