// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-hci.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <soc/mt8167/mt8167-hw.h>
#include <soc/mt8167/mt8167-usb.h>
#include <zxtest/zxtest.h>

namespace mt_usb_hci {
namespace regs = board_mt8167;

// A testing instance which exposes the Init() method.
class TUsbHci : public UsbHci {
 public:
  using UsbHci::Init;
  using UsbHci::UsbHci;
};

class HciTest : public zxtest::Test {
 protected:
  void SetUp() {
    size_t sz;

    zx::vmo usb;
    ASSERT_OK(zx::vmo::create(MT8167_USB1_LENGTH, 0, &usb));
    ASSERT_OK(usb.get_size(&sz));
    ASSERT_OK(ddk::MmioBuffer::Create(0, sz, std::move(usb), ZX_CACHE_POLICY_UNCACHED, &usb_mmio_));

    zx::vmo phy;
    ASSERT_OK(zx::vmo::create(MT8167_USBPHY_LENGTH, 0, &phy));
    ASSERT_OK(phy.get_size(&sz));
    ASSERT_OK(ddk::MmioBuffer::Create(0, sz, std::move(phy), ZX_CACHE_POLICY_UNCACHED, &phy_mmio_));

    ASSERT_OK(zx_interrupt_create(0, 0, ZX_INTERRUPT_VIRTUAL, intr_.reset_and_get_address()));
  }

  std::optional<ddk::MmioBuffer> usb_mmio_;
  std::optional<ddk::MmioBuffer> phy_mmio_;
  zx::interrupt intr_;
};

TEST_F(HciTest, TestReadEndpointNumber) {
  ddk::MmioView v = usb_mmio_->View(0);
  regs::EPINFO::Get().FromValue(0x33).WriteTo(&v);

  TUsbHci hci(fake_ddk::kFakeParent, *std::move(usb_mmio_), *std::move(phy_mmio_), intr_.release());

  EXPECT_OK(hci.Init());
  EXPECT_EQ(3, regs::INDEX::Get().ReadFrom(&v).selected_endpoint());
  hci.DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));
}

TEST_F(HciTest, DdkLifecycle) {
  ddk::MmioView v = usb_mmio_->View(0);
  regs::EPINFO::Get().FromValue(0x33).WriteTo(&v);

  fake_ddk::Bind ddk;
  TUsbHci hci(fake_ddk::kFakeParent, *std::move(usb_mmio_), *std::move(phy_mmio_), intr_.release());

  EXPECT_OK(hci.Init());
  hci.DdkAdd("mt-usb-host");
  hci.DdkAsyncRemove();
  EXPECT_OK(ddk.WaitUntilRemove());
  EXPECT_TRUE(ddk.Ok());
}

}  // namespace mt_usb_hci

int main(int argc, char* argv[]) { return RUN_ALL_TESTS(argc, argv); }
