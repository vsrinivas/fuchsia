// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_MAPPER_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_MAPPER_H_

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stddef.h>

class Mapper {
 public:
  // The given |vmar| must remain valid for the lifetime of the |Mapper| object.
  Mapper(const zx::vmar* vmar);
  ~Mapper();

  zx_status_t Map(zx_vm_option_t options, const zx::vmo& vmo, uint64_t offset, size_t size);
  zx_status_t Unmap();

  std::byte* data() const { return data_; }

 private:
  const zx::vmar* vmar_ = nullptr;
  zx_vaddr_t start_ = 0u;
  size_t size_ = 0u;
  std::byte* data_ = nullptr;
};

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_MAPPER_H_
