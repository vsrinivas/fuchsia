// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/pdev.h>

#include <zxtest/zxtest.h>

#include "zircon/errors.h"

namespace ddk {

class PDevTest : public PDev {
 public:
  explicit PDevTest(pdev_protocol_t* proto) : PDev(proto) {}
};

// Fake functions
zx_status_t mmio_fn(void* ctx, uint32_t index, pdev_mmio_t* out_mmio) { return ZX_OK; }
zx_status_t interrupt_fn(void* ctx, uint32_t index, uint32_t flags, zx_handle_t* out_irq) {
  return ZX_OK;
}
zx_status_t bti_fn(void* ctx, uint32_t index, zx_handle_t* out_bti) { return ZX_OK; }
zx_status_t smc_fn(void* ctx, uint32_t index, zx_handle_t* out_smc) { return ZX_OK; }
zx_status_t device_info_fn(void* ctx, pdev_device_info_t* out_info) { return ZX_OK; }
zx_status_t board_info_fn(void* ctx, pdev_board_info_t* out_info) { return ZX_OK; }

TEST(DdkTest, GetInterrupt) {
  auto fake_ops = pdev_protocol_ops_t{.get_mmio = &mmio_fn,
                                      .get_interrupt = &interrupt_fn,
                                      .get_bti = &bti_fn,
                                      .get_smc = &smc_fn,
                                      .get_device_info = &device_info_fn,
                                      .get_board_info = &board_info_fn};

  auto fake_proto = pdev_protocol_t{.ops = &fake_ops, .ctx = nullptr};

  PDevTest pdev(&fake_proto);
  zx::interrupt out;
  EXPECT_OK(pdev.GetInterrupt(0, 0, &out));
}

}  // namespace ddk

int main(int argc, char** argv) { return RUN_ALL_TESTS(argc, argv); }
