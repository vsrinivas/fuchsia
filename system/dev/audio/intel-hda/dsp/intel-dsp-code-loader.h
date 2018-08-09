// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <lib/zx/vmo.h>
#include <lib/fzl/vmo-mapper.h>

#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/pinned-vmo.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {

class IntelDspCodeLoader : public fbl::RefCounted<IntelDspCodeLoader> {
public:
    IntelDspCodeLoader(adsp_code_loader_registers_t* regs, const fbl::RefPtr<RefCountedBti>& pci_bti);
    ~IntelDspCodeLoader();

    // Hardware allows buffer descriptor lists (BDLs) to be up to 256
    // entries long.
    static constexpr size_t MAX_BDL_LENGTH = 256;

    const char* log_prefix() const { return log_prefix_; }

    void DumpRegisters();

    zx_status_t Initialize() __WARN_UNUSED_RESULT;
    zx_status_t TransferFirmware(const PinnedVmo& pinned_fw, size_t fw_size);
    void StopTransfer();

private:
    // Log prefix storage
    char log_prefix_[LOG_PREFIX_STORAGE] = { 0 };

    // Buffer descriptor list
    // TODO(yky) Look into factoring BDL functionality out to a utility class,
    // because it is shared between the code loader and stream DMA.
    fzl::VmoMapper bdl_cpu_mem_;
    PinnedVmo bdl_dsp_mem_;

    // Registers
    adsp_code_loader_registers_t* regs_ = nullptr;

    // A reference to our controller's BTI. We will need this to grant the controller
    // access to the BDLs and memory holding the DSP firmware.
    const fbl::RefPtr<RefCountedBti> pci_bti_;
};

}  // namespace intel_hda
}  // namespace audio
