// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_DRIVER_H_
#define ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_DRIVER_H_

#include <lib/async/dispatcher.h>
#include <threads.h>

namespace sysmem_driver {

// Placeholder Driver ctx structure.  Not used for anything yet.
class Driver {
 public:
  async_dispatcher_t* dispatcher;
  thrd_t dispatcher_thrd;

 private:
};

}  // namespace sysmem_driver

#endif  // ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_DRIVER_H_
