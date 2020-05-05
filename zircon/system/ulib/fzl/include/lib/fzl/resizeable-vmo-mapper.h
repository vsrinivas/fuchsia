// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FZL_RESIZEABLE_VMO_MAPPER_H_
#define LIB_FZL_RESIZEABLE_VMO_MAPPER_H_

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <fbl/macros.h>

namespace fzl {

// ResizeableVmoMapper is an extension of the basic OwnedVmoMapper utility which
// allows resizing of the mapping after it has been created.
//
class ResizeableVmoMapper : protected OwnedVmoMapper {
 public:
  static std::unique_ptr<ResizeableVmoMapper> Create(
      uint64_t size, const char* name,
      zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
      fbl::RefPtr<VmarManager> vmar_manager = nullptr, uint32_t cache_policy = 0);

  ResizeableVmoMapper() = default;
  ~ResizeableVmoMapper() { Reset(); }
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ResizeableVmoMapper);

  // Move support
  ResizeableVmoMapper(ResizeableVmoMapper&& other) { MoveFromOther(&other); }

  ResizeableVmoMapper& operator=(ResizeableVmoMapper&& other) {
    Reset();
    MoveFromOther(&other);
    return *this;
  }

  // See |VmoMapper::CreateAndMap|
  zx_status_t CreateAndMap(uint64_t size, const char* name,
                           zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                           fbl::RefPtr<VmarManager> vmar_manager = nullptr,
                           uint32_t cache_policy = 0);

  // See |VmoMapper::Map|
  zx_status_t Map(zx::vmo vmo, uint64_t size = 0,
                  zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                  fbl::RefPtr<VmarManager> vmar_manager = nullptr);

  // Attempts to reduce both the VMO size and the VMAR mapping from |size_| to |size|.
  //
  // The start address of the mapping, |start_|, remains unchanged as a result of this call.
  //
  // Failure conditions:
  // Attempting to shrink the mapping to a size of zero or requesting a "shrink" that would increase
  // the mapping size returns an error.
  //
  // If |size| is not page-aligned, shrinking will fail.
  //
  // On failure, the mapping will be safe to use, but will remain at its original size.
  zx_status_t Shrink(size_t size);

  // Attempts to increase both the VMO size and the VMAR mapping from |size_| to |size|.
  //
  // Note that the VMAR mapping, and hence its start address, |start_|, might change. So it is not
  // safe to retain any pointers computed using the old mapping's addresses. The client is
  // expected to ensure their usage of the mapping is safe across a call to Grow().
  //
  // Unlike shrinking, it's permissible to grow to a non-page-aligned |size|.
  //
  // Failure conditions:
  // Attempting to grow the mapping to a size smaller than the current size will return an error.
  //
  // On failure, the mapping will be safe to use, but will remain at its original size.
  zx_status_t Grow(size_t size);

  using OwnedVmoMapper::Release;
  using OwnedVmoMapper::Reset;
  using OwnedVmoMapper::vmo;
  using VmoMapper::manager;
  using VmoMapper::size;
  using VmoMapper::start;

 private:
  void MoveFromOther(ResizeableVmoMapper* other) {
    map_options_ = other->map_options_;
    OwnedVmoMapper::MoveFromOther(other);
  }

  zx_vm_option_t map_options_ = 0;
};

}  // namespace fzl

#endif  // LIB_FZL_RESIZEABLE_VMO_MAPPER_H_
