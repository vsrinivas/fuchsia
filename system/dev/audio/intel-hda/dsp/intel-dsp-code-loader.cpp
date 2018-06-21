// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/limits.h>
#include <zircon/assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <hw/arch_ops.h>
#include <zircon/process.h>

#include "debug-logging.h"
#include "intel-dsp-code-loader.h"

namespace audio {
namespace intel_hda {

namespace {
constexpr uint32_t ADSP_CLDMA_STREAM_TAG = 1;
constexpr uint32_t DMA_ALIGN = 128;
constexpr uint32_t DMA_ALIGN_MASK = DMA_ALIGN - 1;
}  // anon namespace

void IntelDspCodeLoader::DumpRegisters() {
    LOG(INFO, "CTL_STS=0x%08x\n", REG_RD(&regs_->stream.ctl_sts.w));
    LOG(INFO, "   LPIB=0x%08x\n", REG_RD(&regs_->stream.lpib));
    LOG(INFO, "    CBL=0x%08x\n", REG_RD(&regs_->stream.cbl));
    LOG(INFO, "    LVI=0x%04x\n", REG_RD(&regs_->stream.lvi));
    LOG(INFO, "  FIFOD=0x%04x\n", REG_RD(&regs_->stream.fifod));
    LOG(INFO, "    FMT=0x%04x\n", REG_RD(&regs_->stream.fmt));
    LOG(INFO, "   BDPL=0x%08x\n", REG_RD(&regs_->stream.bdpl));
    LOG(INFO, "   BDPU=0x%08x\n", REG_RD(&regs_->stream.bdpu));
    LOG(INFO, " SPBFCH=0x%08x\n", REG_RD(&regs_->spbfch));
    LOG(INFO, "SPBFCTL=0x%08x\n", REG_RD(&regs_->spbfctl));
    LOG(INFO, "   SPIB=0x%08x\n", REG_RD(&regs_->spib));
}

IntelDspCodeLoader::IntelDspCodeLoader(adsp_code_loader_registers_t* regs,
                                       const fbl::RefPtr<RefCountedBti>& pci_bti)
    : regs_(regs),
      pci_bti_(pci_bti) {
    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP Code Loader");
}

IntelDspCodeLoader::~IntelDspCodeLoader() {
}

zx_status_t IntelDspCodeLoader::Initialize() {
    // BDL entries should be 16 bytes long, meaning that we should be able to
    // fit 256 of them perfectly into a single 4k page.
    constexpr size_t MAX_BDL_BYTES = sizeof(IntelHDABDLEntry) * MAX_BDL_LENGTH;
    static_assert(MAX_BDL_BYTES <= PAGE_SIZE, "A max length BDL must fit inside a single page!");

    // Create a VMO made of a single page and map it for read/write so the CPU
    // has access to it.
    constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    zx::vmo bdl_vmo;
    zx_status_t res;
    res = bdl_cpu_mem_.CreateAndMap(PAGE_SIZE,
                                    CPU_MAP_FLAGS,
                                    NULL,
                                    &bdl_vmo,
                                    ZX_RIGHT_SAME_RIGHTS,
                                    ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to create and map %u bytes for code loader BDL (res %d)\n",
            PAGE_SIZE, res);
        return res;
    }

    // Pin this VMO and grant the controller access to it.  The controller
    // should only need read access to buffer descriptor lists.
    constexpr uint32_t DSP_MAP_FLAGS = ZX_BTI_PERM_READ;
    ZX_DEBUG_ASSERT(pci_bti_ != nullptr);
    res = bdl_dsp_mem_.Pin(bdl_vmo, pci_bti_->initiator(), DSP_MAP_FLAGS);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to pin pages for code loader BDL (res %d)\n", res);
        return res;
    }

    // Sanity checks.  At this point, everything should be allocated, mapped,
    // and should obey the alignment restrictions imposed by the HDA spec.
    ZX_DEBUG_ASSERT(bdl_cpu_mem_.start() != 0);
    ZX_DEBUG_ASSERT(!(reinterpret_cast<uintptr_t>(bdl_cpu_mem_.start()) & DMA_ALIGN_MASK));
    ZX_DEBUG_ASSERT(bdl_dsp_mem_.region_count() == 1);
    ZX_DEBUG_ASSERT(!(bdl_dsp_mem_.region(0).phys_addr & DMA_ALIGN_MASK));

    return ZX_OK;
}

zx_status_t IntelDspCodeLoader::TransferFirmware(const PinnedVmo& pinned_fw, size_t fw_size) {

    uint32_t region_count = pinned_fw.region_count();

    // Sanity checks that the firmware fits in the BDL.
    ZX_DEBUG_ASSERT(region_count < MAX_BDL_LENGTH);

    // Build BDL to transfer the firmware
    IntelHDABDLEntry* bdl = reinterpret_cast<IntelHDABDLEntry*>(bdl_cpu_mem_.start());
    size_t remaining = fw_size;
    uint32_t num_region;
    for (num_region = 0; (num_region < region_count) && (remaining > 0); num_region++) {
        const auto& r = pinned_fw.region(num_region);

        if (r.size > fbl::numeric_limits<uint32_t>::max()) {
            LOG(ERROR, "VMO region too large (%" PRIu64 " bytes)\n", r.size);
            return ZX_ERR_INTERNAL;
        }

        bdl[num_region].address = r.phys_addr;
        bdl[num_region].length = (remaining > r.size) ? static_cast<uint32_t>(r.size) :
                                                        static_cast<uint32_t>(remaining);
        remaining -= bdl[num_region].length;
    }

    // Interrupt on the last BDL entry
    bdl[num_region - 1].flags = IntelHDABDLEntry::IOC_FLAG;

    // Program DMA
    const auto bdl_phys = bdl_dsp_mem_.region(0).phys_addr;

    uint32_t ctl_val = HDA_SD_REG_CTRL_STRM_TAG(ADSP_CLDMA_STREAM_TAG) |
                       HDA_SD_REG_CTRL_STRIPE1;
    REG_WR(&regs_->stream.ctl_sts.w, ctl_val);
    REG_WR(&regs_->stream.bdpl, static_cast<uint32_t>(bdl_phys & 0xFFFFFFFFu));
    REG_WR(&regs_->stream.bdpu, static_cast<uint32_t>((bdl_phys >> 32) & 0xFFFFFFFFu));
    REG_WR(&regs_->stream.cbl, fw_size);
    REG_WR(&regs_->stream.lvi, region_count - 1);

    REG_WR(&regs_->spbfctl, ADSP_REG_CL_SPBFCTL_SPIBE);
    REG_WR(&regs_->spib, fw_size);

    hw_wmb();

    // Start DMA
    constexpr uint32_t SET = HDA_SD_REG_CTRL_RUN  |
                             HDA_SD_REG_CTRL_IOCE |
                             HDA_SD_REG_CTRL_FEIE |
                             HDA_SD_REG_CTRL_DEIE |
                             HDA_SD_REG_STS32_ACK;
    REG_SET_BITS(&regs_->stream.ctl_sts.w, SET);
    hw_wmb();

    return ZX_OK;
}

void IntelDspCodeLoader::StopTransfer() {
    REG_CLR_BITS(&regs_->stream.ctl_sts.w, HDA_SD_REG_CTRL_RUN);
}

}  // namespace intel_hda
}  // namespace audio
