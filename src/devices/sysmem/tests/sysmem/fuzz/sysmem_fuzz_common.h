// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_SYSMEM_FUZZ_COMMON_H_
#define SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_SYSMEM_FUZZ_COMMON_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <lib/fake_ddk/fake_ddk.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/sysmem/drivers/sysmem/device.h"
#include "src/devices/sysmem/drivers/sysmem/driver.h"

class FakeDdkSysmem {
 public:
  ~FakeDdkSysmem();
  fake_ddk::Bind& ddk() { return ddk_; }

  bool Init();
  zx::result<fidl::ClientEnd<fuchsia_sysmem::Allocator>> Connect();

 protected:
  bool initialized_ = false;
  sysmem_driver::Driver sysmem_ctx_;
  sysmem_driver::Device sysmem_{fake_ddk::kFakeParent, &sysmem_ctx_};

  fake_pdev::FakePDev pdev_;
  // ddk must be destroyed before sysmem because it may be executing messages against sysmem on
  // another thread.
  fake_ddk::Bind ddk_;
};

#endif  // SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_SYSMEM_FUZZ_COMMON_H_
