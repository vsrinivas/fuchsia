// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_SYSMEM_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_SYSMEM_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>

#include "lib/fidl/llcpp/connect_service.h"

namespace compat {

class Driver;

// This emulates the "sysmem" device.
// TODO(fxbug.dev/93333): Remove this once we have proper composite support in the compat driver.
class Sysmem : public ddk::SysmemProtocol<Sysmem> {
 public:
  explicit Sysmem(Driver* driver)
      : driver_(driver),
        proto_{
            .ops = &sysmem_protocol_ops_,
            .ctx = this,
        } {}

  zx_status_t SysmemConnect(zx::channel allocator_request);
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection);

  zx_status_t SysmemRegisterSecureMem(zx::channel secure_mem_connection);
  zx_status_t SysmemUnregisterSecureMem();

  sysmem_protocol_t* protocol() { return &proto_; }

 private:
  Driver* driver_;
  sysmem_protocol_t proto_;
};

}  // namespace compat

#endif
