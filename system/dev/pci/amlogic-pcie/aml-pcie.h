// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/mmio.h>
#include <dev/pci/designware/dw-pcie.h>
#include <fbl/unique_ptr.h>

namespace pcie {
namespace aml {

const uint32_t kRstPcieA = (0x1 << 1);
const uint32_t kRstPcieB = (0x1 << 2);
const uint32_t kRstPcieApb = (0x1 << 6);
const uint32_t kRstPciePhy = (0x1 << 7);

// The Aml Pcie controller is an instance of the DesignWare IP
class AmlPcie : public designware::DwPcie {
  public:
      AmlPcie(
          fbl::unique_ptr<ddk::MmioBuffer> elbi,
          fbl::unique_ptr<ddk::MmioBuffer> cfg,
          fbl::unique_ptr<ddk::MmioBuffer> rst,
          const uint32_t nLanes)
          : designware::DwPcie(fbl::move(elbi), fbl::move(cfg), nLanes), rst_(fbl::move(rst)) {}

      void AssertReset(const uint32_t bits);
      void ClearReset(const uint32_t bits);

      zx_status_t EstablishLink(
          const iatu_translation_entry_t* cfg,
          const iatu_translation_entry_t* io,
          const iatu_translation_entry_t* mem);

  private:
    bool IsLinkUp() override;

    void PcieInit();
    void SetMaxPayload(const uint32_t size);
    void SetMaxReadRequest(const uint32_t size);
    void EnableMemorySpace();
    zx_status_t AwaitLinkUp();
    void ConfigureRootBridge(const iatu_translation_entry_t* mem);
    void RmwCtrlSts(const uint32_t size, const uint32_t shift, const uint32_t mask);

    fbl::unique_ptr<ddk::MmioBuffer> rst_;
};

}  // namespace aml
}  // namespace pcie
