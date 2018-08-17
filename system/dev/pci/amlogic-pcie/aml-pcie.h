// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <dev/pci/designware/dw-pcie.h>

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
        void* elbi,
        void* cfg,
        void* rst,
        const uint32_t nLanes
    ) : designware::DwPcie(elbi, cfg, nLanes)
      , rst_(rst) {}

    void AssertReset(const uint32_t bits);
    void ClearReset(const uint32_t bits);

    zx_status_t EstablishLink(
        const iatu_translation_entry_t* cfg,
        const iatu_translation_entry_t* io,
        const iatu_translation_entry_t* mem
    );

  private:
    bool IsLinkUp() override;

    void PcieInit();
    void SetMaxPayload(const uint32_t size);
    void SetMaxReadRequest(const uint32_t size);
    void EnableMemorySpace();
    zx_status_t AwaitLinkUp();
    void ConfigureRootBridge();
    void RmwCtrlSts(const uint32_t size, const uint32_t shift, const uint32_t mask);

    volatile void* rst_;
};

}  // namespace aml
}  // namespace pcie