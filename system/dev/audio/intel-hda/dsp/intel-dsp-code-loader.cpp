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
constexpr uint32_t EXT_MANIFEST_HDR_MAGIC = 0x31454124;
constexpr uint32_t DMA_ALIGN = 128;
constexpr uint32_t DMA_ALIGN_MASK = DMA_ALIGN - 1;
}  // anon namespace

struct skl_adspfw_ext_manifest_hdr_t {
    uint32_t id;
    uint32_t len;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t entries;
} __PACKED;

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

zx_status_t IntelDspCodeLoader::StripFirmware(const zx::vmo& fw, void* out, size_t* size_inout) {
    ZX_DEBUG_ASSERT(out != nullptr);
    ZX_DEBUG_ASSERT(size_inout != nullptr);

    // Check for extended manifest
    skl_adspfw_ext_manifest_hdr_t hdr;
    zx_status_t st = fw.read(&hdr, 0, sizeof(hdr));
    if (st != ZX_OK) {
        return st;
    }

    // If the firmware contains an extended manifest, it must be stripped
    // before loading to the DSP.
    uint32_t offset = 0;
    if (hdr.id == EXT_MANIFEST_HDR_MAGIC) {
        offset = hdr.len;
    }

    // Always copy the firmware to simplify the code.
    size_t bytes = *size_inout - offset;
    if (*size_inout < bytes) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    *size_inout = bytes;
    return fw.read(out, offset, bytes);
}

zx_status_t IntelDspCodeLoader::TransferFirmware(const zx::vmo& fw, size_t fw_size) {
    // The max length of the firmware is 256 pages, assuming a fully distinguous VMO.
    constexpr size_t MAX_FW_BYTES = PAGE_SIZE * MAX_BDL_LENGTH;
    if (fw_size > MAX_FW_BYTES) {
        LOG(ERROR, "DSP firmware is too big (0x%zx bytes > 0x%zx bytes)\n", fw_size, MAX_FW_BYTES);
        return ZX_ERR_INVALID_ARGS;
    }

    // Create and map a VMO to copy the firmware into. The firmware must be copied to
    // a new VMO because BDL addresses must be 128-byte aligned, and the presence
    // of the extended manifest header will guarantee un-alignment.
    // This VMO is mapped once and thrown away after firmware loading, so map it
    // into the root VMAR so we don't need to allocate more space in DriverVmars::registers().
    constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    zx::vmo stripped_vmo;
    fbl::VmoMapper stripped_fw;
    zx_status_t st = stripped_fw.CreateAndMap(fw_size, CPU_MAP_FLAGS, nullptr, &stripped_vmo);
    if (st != ZX_OK) {
        LOG(ERROR, "Error creating DSP firmware VMO (err %d)\n", st);
        return st;
    }

    size_t stripped_size = fw_size;
    st = StripFirmware(fw, stripped_fw.start(), &stripped_size);
    if (st != ZX_OK) {
        LOG(ERROR, "Error stripping DSP firmware (err %d)\n", st);
        return st;
    }

    // Pin this VMO and grant the controller access to it.  The controller
    // should only need read access to the firmware.
    constexpr uint32_t DSP_MAP_FLAGS = ZX_BTI_PERM_READ;
    PinnedVmo pinned_fw;
    ZX_DEBUG_ASSERT(pci_bti_ != nullptr);
    st = pinned_fw.Pin(stripped_vmo, pci_bti_->initiator(), DSP_MAP_FLAGS);
    if (st != ZX_OK) {
        LOG(ERROR, "Failed to pin pages for DSP firmware (res %d)\n", st);
        return st;
    }

    uint32_t region_count = pinned_fw.region_count();

    // Sanity checks that the firmware fits in the BDL.
    ZX_DEBUG_ASSERT(region_count < MAX_BDL_LENGTH);

    // Build BDL to transfer the firmware
    IntelHDABDLEntry* bdl = reinterpret_cast<IntelHDABDLEntry*>(bdl_cpu_mem_.start());
    uint32_t num_region;
    for (num_region = 0; num_region < region_count; num_region++) {
        const auto& r = pinned_fw.region(num_region);

        if (r.size > fbl::numeric_limits<uint32_t>::max()) {
            LOG(ERROR, "VMO region too large (%" PRIu64 " bytes)\n", r.size);
            return ZX_ERR_INTERNAL;
        }

        bdl[num_region].address = r.phys_addr;
        bdl[num_region].length  = static_cast<uint32_t>(r.size);
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

    // TODO(yky) I don't know why I need this
    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));

    return ZX_OK;
}

}  // namespace intel_hda
}  // namespace audio
