// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_CONFIG_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_CONFIG_H_

#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/pci.h>

#include <cstdint>

#include <fbl/algorithm.h>

#include "../../common.h"
#include "../../config.h"
#include "test_device.h"

namespace pci {

// For most operations a real MmioConfig is fine for working with
// a fake ecam. However, for BAR probing we need to mock the side
// effects of the writes that are used to determine the size of
// the BAR. Fortunately, these writes are always UINT32_MAX and can
// be caught because they are not otherwise valid. If necessary, this
// class can be extended to handle other side-effects as well.

class FakeMmioConfig final : public MmioConfig {
 public:
  FakeMmioConfig(pci_bdf_t bdf, ddk::MmioView&& view) : MmioConfig(bdf, std::move(view)) {}
  void MockBarProbeSideEffects(uint32_t bar_id) const {
    uint32_t bar_val = MmioConfig::Read(Config::kBar(bar_id));
    auto reg = config::BaseAddress::Get().FromValue(bar_val);

    // When probing a BAR, the hardware writes a 0 in every valid bit used
    // for an address. The least significant address bit set represents the
    // size of the BAR. For example, if our size was 1M we would have an
    // unmanipulated 0x00100000
    //
    // 0x00100000 - 1 = 0x000FFFFF
    //  ~(0x000FFFFF) = 0xFFF00000

    uint32_t size = kTestDeviceBars[bar_id].size;
    ZX_ASSERT(size == 0 || fbl::is_pow2(size));
    size = ~(size - 1);

    if (reg.is_io_space()) {
      reg.io()->set_base_address(size);
    } else {
      reg.mmio()->set_base_address(size);
    }
    MmioConfig::Write(Config::kBar(bar_id), reg.reg_value());
  }

  void Write(const PciReg32 addr, uint32_t val) const final {
    switch (addr.offset()) {
      case Config::kBar(0).offset():
      case Config::kBar(1).offset():
      case Config::kBar(2).offset():
      case Config::kBar(3).offset():
      case Config::kBar(4).offset():
      case Config::kBar(5).offset(): {
        // A 32-bit write of all 1s in these addresses is reserved for
        // querying the BAR size as long as it's not the upper half of
        // a 64 bit register.
        uint32_t bar_id = static_cast<uint32_t>((addr.offset() - Config::kBar(0).offset()) / 4);
        if (val == UINT32_MAX && !kTestDeviceBars[bar_id].is_upper_half) {
          MockBarProbeSideEffects(bar_id);
          break;
        }
        __FALLTHROUGH;
      }
      default:
        return MmioConfig::Write(addr, val);
    }
  }

  const char* type(void) const final { return "fake_mmio"; }
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_CONFIG_H_
