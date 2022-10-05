// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_POOL_MEM_CONFIG_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_POOL_MEM_CONFIG_H_

#include "item-base.h"

// Forward declaration for <lib/memalloc/pool.h>.
namespace memalloc {
class Pool;
}  // namespace memalloc

namespace boot_shim {

class PoolMemConfigItem : public ItemBase {
 public:
  void Init(const memalloc::Pool& pool) { pool_ = &pool; }

  size_t size_bytes() const;

  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) const;

 private:
  const memalloc::Pool* pool_ = nullptr;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_POOL_MEM_CONFIG_H_
