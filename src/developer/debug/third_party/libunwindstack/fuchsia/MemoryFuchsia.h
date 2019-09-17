// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_MEMORY_FUCHSIA_H_
#define GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_MEMORY_FUCHSIA_H_

#include <zircon/types.h>

#include "unwindstack/Memory.h"

namespace unwindstack {

class MemoryFuchsia final : public Memory {
 public:
  // The handle must outlive this class.
  explicit MemoryFuchsia(zx_handle_t process);

  size_t Read(uint64_t addr, void* dst, size_t size) final;

 private:
  const zx_handle_t process_;
};

}  // namespace unwindstack

#endif  // GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_MEMORY_FUCHSIA_H_
