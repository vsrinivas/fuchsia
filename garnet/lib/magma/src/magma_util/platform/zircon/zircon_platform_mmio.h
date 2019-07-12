// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/mmio-buffer.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"

namespace magma {

class ZirconPlatformMmio : public PlatformMmio {
 public:
  ZirconPlatformMmio(mmio_buffer_t mmio);

  ~ZirconPlatformMmio();
  bool Pin(zx_handle_t bti);
  uint64_t physical_address() override;

 private:
  mmio_buffer_t mmio_;
  mmio_pinned_buffer_t pinned_mmio_ = {};
};

}  // namespace magma
