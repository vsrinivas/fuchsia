// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/arch_ops.h>

#include "debug-logging.h"
#include "intel-hda-controller.h"
#include "intel-hda-stream.h"
#include "thread-annotations.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

namespace {
static constexpr zx_time_t INTEL_HDA_RESET_HOLD_TIME_NSEC        = ZX_USEC(100); // Section 5.5.1.2
static constexpr zx_time_t INTEL_HDA_RESET_TIMEOUT_NSEC          = ZX_MSEC(1);   // 1mS Arbitrary
static constexpr zx_time_t INTEL_HDA_RING_BUF_RESET_TIMEOUT_NSEC = ZX_MSEC(1);   // 1mS Arbitrary
static constexpr zx_time_t INTEL_HDA_RESET_POLL_TIMEOUT_NSEC     = ZX_USEC(10);  // 10uS Arbitrary
static constexpr zx_time_t INTEL_HDA_CODEC_DISCOVERY_WAIT_NSEC   = ZX_USEC(521); // Section 4.3
static constexpr size_t CMD_BUFFER_SIZE = 4096;
}  // anon namespace

zx_status_t IntelHDAController::ResetControllerHW() {
    zx_status_t res;

    // Are we currently being held in reset?  If not, try to make sure that all
    // of our DMA streams are stopped and have been reset (but are not being
    // held in reset) before cycling the controller.  Anecdotally, holding a
    // stream in reset while attempting to reset the controller on some Skylake
    // hardware has caused some pretty profound hardware lockups which require
    // fully removing power (warm reboot == not good enough) to recover from.
    if (REG_RD(&regs_->gctl) & HDA_REG_GCTL_HWINIT) {
        // Explicitly disable all top level interrupt sources.
        REG_WR(&regs_->intsts, 0u);
        hw_mb();

        // Count the number of streams present in the hardware and
        // unconditionally stop and reset all of them.
        uint16_t gcap = REG_RD(&regs_->gcap);
        unsigned int total_stream_cnt = HDA_REG_GCAP_ISS(gcap)
                                      + HDA_REG_GCAP_OSS(gcap)
                                      + HDA_REG_GCAP_BSS(gcap);

        if (total_stream_cnt > countof(regs_->stream_desc)) {
            LOG("Fatal error during reset!  Controller reports more streams (%u) "
                "than should be possible for IHDA hardware.  (GCAP = 0x%04hx)\n",
                total_stream_cnt, gcap);
            return ZX_ERR_INTERNAL;
        }

        hda_stream_desc_regs_t* sregs = regs_->stream_desc;
        for (uint32_t i = 0; i < total_stream_cnt; ++i) {
            IntelHDAStream::Reset(sregs + i);
        }

        // Explicitly shut down any CORB/RIRB DMA
        REG_WR(&regs_->corbctl, 0u);
        REG_WR(&regs_->rirbctl, 0u);
    }

    // Assert the reset signal and wait for the controller to ack.
    REG_CLR_BITS(&regs_->gctl, HDA_REG_GCTL_HWINIT);
    hw_mb();

    res = WaitCondition(INTEL_HDA_RESET_TIMEOUT_NSEC,
                        INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                        [](void* r) -> bool {
                           auto regs = reinterpret_cast<hda_registers_t*>(r);
                           return (REG_RD(&regs->gctl) & HDA_REG_GCTL_HWINIT) == 0;
                        },
                        regs_);

    if (res != ZX_OK) {
        LOG("Error attempting to enter reset! (res %d)\n", res);
        return res;
    }

    // Wait the spec mandated hold time.
    zx_nanosleep(zx_deadline_after(INTEL_HDA_RESET_HOLD_TIME_NSEC));

    // Deassert the reset signal and wait for the controller to ack.
    REG_SET_BITS<uint32_t>(&regs_->gctl, HDA_REG_GCTL_HWINIT);
    hw_mb();

    res = WaitCondition(INTEL_HDA_RESET_TIMEOUT_NSEC,
                        INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                        [](void* r) -> bool {
                           auto regs = reinterpret_cast<hda_registers_t*>(r);
                           return (REG_RD(&regs->gctl) & HDA_REG_GCTL_HWINIT) != 0;
                        },
                        regs_);

    if (res != ZX_OK) {
        LOG("Error attempting to leave reset! (res %d)\n", res);
        return res;
    }

    // Wait the spec mandated discovery time.
    zx_nanosleep(zx_deadline_after(INTEL_HDA_CODEC_DISCOVERY_WAIT_NSEC));
    return res;
}

zx_status_t IntelHDAController::ResetCORBRdPtrLocked() {
    zx_status_t res;

    /* Set the reset bit, then wait for ack from the HW.  See Section 3.3.21 */
    REG_WR(&regs_->corbrp, HDA_REG_CORBRP_RST);
    hw_mb();

    if ((res = WaitCondition(INTEL_HDA_RING_BUF_RESET_TIMEOUT_NSEC,
                             INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                             [](void* r) -> bool {
                                auto regs = reinterpret_cast<hda_registers_t*>(r);
                                return (REG_RD(&regs->corbrp) & HDA_REG_CORBRP_RST) != 0;
                             },
                             regs_)) != ZX_OK) {
        return res;
    }

    /* Clear the reset bit, then wait for ack */
    REG_WR(&regs_->corbrp, 0u);
    hw_mb();

    if ((res = WaitCondition(INTEL_HDA_RING_BUF_RESET_TIMEOUT_NSEC,
                             INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                             [](void* r) -> bool {
                                auto regs = reinterpret_cast<hda_registers_t*>(r);
                                return (REG_RD(&regs->corbrp) & HDA_REG_CORBRP_RST) == 0;
                             },
                             regs_)) != ZX_OK) {
        return res;
    }

    return ZX_OK;
}

zx_status_t IntelHDAController::SetupPCIDevice(zx_device_t* pci_dev) {
    zx_status_t res;

    if (pci_dev == nullptr)
        return ZX_ERR_INVALID_ARGS;

    // Have we already been set up?
    if (pci_dev_ != nullptr) {
        LOG("Device already initialized!\n");
        return ZX_ERR_BAD_STATE;
    }

    ZX_DEBUG_ASSERT(irq_handle_  == ZX_HANDLE_INVALID);
    ZX_DEBUG_ASSERT(regs_handle_ == ZX_HANDLE_INVALID);
    ZX_DEBUG_ASSERT(pci_.ops == nullptr);

    pci_dev_ = pci_dev;

    // Generate a default debug tag for now.
    snprintf(debug_tag_, sizeof(debug_tag_), "IHDA Controller (unknown BDF)");

    // The device had better be a PCI device, or we are very confused.
    res = device_get_protocol(pci_dev_, ZX_PROTOCOL_PCI, reinterpret_cast<void*>(&pci_));
    if (res != ZX_OK) {
        LOG("PCI device does not support PCI protocol! (res %d)\n", res);
        return res;
    }

    // Fetch our device info and use it to re-generate our debug tag once we
    // know our BDF address.
    ZX_DEBUG_ASSERT(pci_.ops != nullptr);
    res = pci_.ops->get_device_info(pci_.ctx, &pci_dev_info_);
    if (res != ZX_OK) {
        LOG("Failed to fetch basic PCI device info! (res %d)\n", res);
        return res;
    }

    snprintf(debug_tag_, sizeof(debug_tag_), "IHDA Controller %02x:%02x.%01x",
             pci_dev_info_.bus_id,
             pci_dev_info_.dev_id,
             pci_dev_info_.func_id);


    // Map in the registers located at BAR 0.  Make sure that they are the size
    // we expect them to be.
    ZX_DEBUG_ASSERT(regs_handle_ == ZX_HANDLE_INVALID);
    uint64_t reg_window_size;
    hda_all_registers_t* all_regs;
    res = pci_.ops->map_resource(pci_.ctx,
                               PCI_RESOURCE_BAR_0,
                               ZX_CACHE_POLICY_UNCACHED_DEVICE,
                               reinterpret_cast<void**>(&all_regs),
                               &reg_window_size,
                               &regs_handle_);
    if (res != ZX_OK) {
        LOG("Error attempting to map registers (res %d)\n", res);
        return res;
    }

    if (sizeof(*all_regs) != reg_window_size) {
        LOG("Bad register window size (expected 0x%zx got 0x%" PRIx64 ")\n",
            sizeof(*all_regs), reg_window_size);
        return ZX_ERR_INVALID_ARGS;
    }

    regs_ = &all_regs->regs;

    return ZX_OK;
}

zx_status_t IntelHDAController::SetupPCIInterrupts() {
    ZX_DEBUG_ASSERT(pci_dev_ != nullptr);

    // Configure our IRQ mode and map our IRQ handle.  Try to use MSI, but if
    // that fails, fall back on legacy IRQs.
    zx_status_t res = pci_.ops->set_irq_mode(pci_.ctx, ZX_PCIE_IRQ_MODE_MSI, 1);
    if (res != ZX_OK) {
        res = pci_.ops->set_irq_mode(pci_.ctx, ZX_PCIE_IRQ_MODE_LEGACY, 1);
        if (res != ZX_OK) {
            LOG("Failed to set IRQ mode (%d)!\n", res);
            return res;
        } else {
            LOG("Falling back on legacy IRQ mode!\n");
        }
    }

    ZX_DEBUG_ASSERT(irq_handle_ == ZX_HANDLE_INVALID);
    res = pci_.ops->map_interrupt(pci_.ctx, 0, &irq_handle_);
    if (res != ZX_OK) {
        LOG("Failed to map IRQ! (res %d)\n", res);
        return res;
    }

    // Enable Bus Mastering so we can DMA data and receive MSIs
    res = pci_.ops->enable_bus_master(pci_.ctx, true);
    if (res != ZX_OK) {
        LOG("Failed to enable PCI bus mastering!\n");
        return res;
    }

    return ZX_OK;
}

zx_status_t IntelHDAController::SetupStreamDescriptors() {
    fbl::AutoLock stream_pool_lock(&stream_pool_lock_);

    // Sanity check our stream counts.
    uint16_t gcap;
    unsigned int input_stream_cnt, output_stream_cnt, bidir_stream_cnt, total_stream_cnt;
    gcap              = REG_RD(&regs_->gcap);
    input_stream_cnt  = HDA_REG_GCAP_ISS(gcap);
    output_stream_cnt = HDA_REG_GCAP_OSS(gcap);
    bidir_stream_cnt  = HDA_REG_GCAP_BSS(gcap);
    total_stream_cnt  = input_stream_cnt + output_stream_cnt + bidir_stream_cnt;

    static_assert(IntelHDAStream::MAX_STREAMS_PER_CONTROLLER == countof(regs_->stream_desc),
                  "Max stream count mismatch!");

    if (!total_stream_cnt || (total_stream_cnt > countof(regs_->stream_desc))) {
        LOG("Invalid stream counts in GCAP register (In %u Out %u Bidir %u; Max %zu)\n",
            input_stream_cnt, output_stream_cnt, bidir_stream_cnt, countof(regs_->stream_desc));
        return ZX_ERR_INTERNAL;
    }

    // Allocate and map storage for our buffer descriptor lists.
    //
    // TODO(johngro) : Relax this restriction.  Individual BDLs need to be
    // contiguous in physical memory (and non-swap-able) but the overall
    // allocation does not need to be.
    uint32_t bdl_size, total_bdl_size;

    bdl_size       = sizeof(IntelHDABDLEntry) * IntelHDAStream::MAX_BDL_LENGTH;
    total_bdl_size = bdl_size * total_stream_cnt;

    zx_status_t res = bdl_mem_.Allocate(total_bdl_size);
    if (res != ZX_OK) {
        LOG("Failed to allocate %u bytes of contiguous physical memory for "
            "buffer descriptor lists!  (res %d)\n", total_bdl_size, res);
        return res;
    }

    // Map the memory in so that we can access it.
    res = bdl_mem_.Map();
    if (res != ZX_OK) {
        LOG("Failed to map BDL memory!  (res %d)\n", res);
        return res;
    }

    // Allocate our stream descriptors and populate our free lists.
    for (uint32_t i = 0, bdl_off = 0; i < total_stream_cnt; ++i, bdl_off += bdl_size) {
        uint16_t stream_id = static_cast<uint16_t>(i + 1);
        auto type = (i < input_stream_cnt)
                  ? IntelHDAStream::Type::INPUT
                  : ((i < input_stream_cnt + output_stream_cnt)
                  ? IntelHDAStream::Type::OUTPUT
                  : IntelHDAStream::Type::BIDIR);

        auto stream = fbl::AdoptRef(new IntelHDAStream(type,
                                                        stream_id,
                                                        &regs_->stream_desc[i],
                                                        bdl_mem_.phys() + bdl_off,
                                                        bdl_mem_.virt() + bdl_off));

        ZX_DEBUG_ASSERT(i < countof(all_streams_));
        ZX_DEBUG_ASSERT(all_streams_[i] == nullptr);
        all_streams_[i] = stream;

        ReturnStreamLocked(fbl::move(stream));
    }

    return ZX_OK;
}

zx_status_t IntelHDAController::SetupCommandBufferSize(uint8_t* size_reg,
                                                       unsigned int* entry_count) {
    // Note: this method takes advantage of the fact that the TX and RX ring
    // buffer size register bitfield definitions are identical.
    uint8_t tmp = REG_RD(size_reg);
    uint8_t cmd;

    if (tmp & HDA_REG_CORBSIZE_CAP_256ENT) {
        *entry_count = 256;
        cmd = HDA_REG_CORBSIZE_CFG_256ENT;
    } else if (tmp & HDA_REG_CORBSIZE_CAP_16ENT) {
        *entry_count = 16;
        cmd = HDA_REG_CORBSIZE_CFG_16ENT;
    } else if (tmp & HDA_REG_CORBSIZE_CAP_2ENT) {
        *entry_count = 2;
        cmd = HDA_REG_CORBSIZE_CFG_2ENT;
    } else {
        LOG("Invalid ring buffer capabilities! (0x%02x)\n", tmp);
        return ZX_ERR_BAD_STATE;
    }

    REG_WR(size_reg, cmd);
    return ZX_OK;
}

zx_status_t IntelHDAController::SetupCommandBuffer() {
    fbl::AutoLock corb_lock(&corb_lock_);
    fbl::AutoLock rirb_lock(&rirb_lock_);
    zx_status_t res;

    // Allocate our command buffer memory and map it into our address space.
    // Even the largest buffers permissible should fit within a single 4k page.
    static_assert(CMD_BUFFER_SIZE >= (HDA_CORB_MAX_BYTES + HDA_RIRB_MAX_BYTES),
                  "CMD_BUFFER_SIZE to small to hold CORB and RIRB buffers!");
    res = cmd_buf_mem_.Allocate(CMD_BUFFER_SIZE);
    if (res != ZX_OK) {
        LOG("Failed to allocate %zu bytes for CORB/RIRB command buffers! (res %d)\n",
            CMD_BUFFER_SIZE, res);
        return res;
    }

    // Now map it so we have access as well.
    res = cmd_buf_mem_.Map();
    if (res != ZX_OK) {
        LOG("Failed to map CORB/RIRB command buffer (res %d)\n", res);
        return res;
    }

    // Start by making sure that the output and response ring buffers are being
    // held in the stopped state
    REG_WR(&regs_->corbctl, 0u);
    REG_WR(&regs_->rirbctl, 0u);

    // Reset the read and write pointers for both ring buffers
    REG_WR(&regs_->corbwp, 0u);
    res = ResetCORBRdPtrLocked();
    if (res != ZX_OK)
        return res;

    // Note; the HW does not expose a Response Input Ring Buffer Read Pointer,
    // we have to maintain our own.
    rirb_rd_ptr_ = 0;
    REG_WR(&regs_->rirbwp, HDA_REG_RIRBWP_RST);

    // Physical memory for the CORB/RIRB should already have been allocated at
    // this point
    ZX_DEBUG_ASSERT(cmd_buf_mem_.virt() != 0);

    // Determine the ring buffer sizes.  If there are options, make them as
    // large as possible.
    res = SetupCommandBufferSize(&regs_->corbsize, &corb_entry_count_);
    if (res != ZX_OK)
        return res;

    res = SetupCommandBufferSize(&regs_->rirbsize, &rirb_entry_count_);
    if (res != ZX_OK)
        return res;

    // Stash these so we don't have to constantly recalculate then
    corb_mask_ = corb_entry_count_ - 1u;
    rirb_mask_ = rirb_entry_count_ - 1u;
    corb_max_in_flight_ = rirb_mask_ > RIRB_RESERVED_RESPONSE_SLOTS
                        ? rirb_mask_ - RIRB_RESERVED_RESPONSE_SLOTS
                        : 1;
    corb_max_in_flight_ = fbl::min(corb_max_in_flight_, corb_mask_);

    // Program the base address registers for the TX/RX ring buffers, and set up
    // the virtual pointers to the ring buffer entries.
    uint64_t cmd_buf_paddr64 = static_cast<uint64_t>(cmd_buf_mem_.phys());

    // TODO(johngro) : If the controller does not support 64 bit phys
    // addressing, we need to make sure to get a page from low memory to use for
    // our command buffers.
    bool gcap_64bit_ok = HDA_REG_GCAP_64OK(REG_RD(&regs_->gcap));
    if ((cmd_buf_paddr64 >> 32) && !gcap_64bit_ok) {
        LOG("Intel HDA controller does not support 64-bit physical addressing!\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Section 4.4.1.1; corb ring buffer base address must be 128 byte aligned.
    ZX_DEBUG_ASSERT(!(cmd_buf_paddr64 & 0x7F));
    REG_WR(&regs_->corblbase, ((uint32_t)(cmd_buf_paddr64 & 0xFFFFFFFF)));
    REG_WR(&regs_->corbubase, ((uint32_t)(cmd_buf_paddr64 >> 32)));
    corb_ = reinterpret_cast<CodecCommand*>(cmd_buf_mem_.virt());

    cmd_buf_paddr64 += HDA_CORB_MAX_BYTES;

    // Section 4.4.2.2; rirb ring buffer base address must be 128 byte aligned.
    ZX_DEBUG_ASSERT(!(cmd_buf_paddr64 & 0x7F));
    REG_WR(&regs_->rirblbase, ((uint32_t)(cmd_buf_paddr64 & 0xFFFFFFFF)));
    REG_WR(&regs_->rirbubase, ((uint32_t)(cmd_buf_paddr64 >> 32)));
    rirb_ = reinterpret_cast<CodecResponse*>(cmd_buf_mem_.virt() + HDA_CORB_MAX_BYTES);

    // Make sure our current view of the space available in the CORB is up-to-date.
    ComputeCORBSpaceLocked();

    // Set the response interrupt count threshold.  The RIRB IRQ will fire any
    // time all of the SDATA_IN lines stop having codec responses to transmit,
    // or when RINTCNT responses have been received, whichever happens
    // first.  We would like to batch up responses to minimize IRQ load, but we
    // also need to make sure to...
    // 1) Not configure the threshold to be larger than the available space in
    //    the ring buffer.
    // 2) Reserve some space (if we can) at the end of the ring buffer so the
    //    hardware has space to write while we are servicing our IRQ.  If we
    //    reserve no space, then the ring buffer is going to fill up and
    //    potentially overflow before we can get in there and process responses.
    unsigned int thresh = rirb_entry_count_ - 1u;
    if (thresh > RIRB_RESERVED_RESPONSE_SLOTS)
        thresh -= RIRB_RESERVED_RESPONSE_SLOTS;
    ZX_DEBUG_ASSERT(thresh);
    REG_WR(&regs_->rintcnt, thresh);

    // Clear out any lingering interrupt status
    REG_WR(&regs_->corbsts, HDA_REG_CORBSTS_MEI);
    REG_WR(&regs_->rirbsts, OR(HDA_REG_RIRBSTS_INTFL, HDA_REG_RIRBSTS_OIS));

    // Enable the TX/RX IRQs and DMA engines.
    REG_WR(&regs_->corbctl, OR(HDA_REG_CORBCTL_MEIE, HDA_REG_CORBCTL_DMA_EN));
    REG_WR(&regs_->rirbctl, OR(OR(HDA_REG_RIRBCTL_INTCTL, HDA_REG_RIRBCTL_DMA_EN),
                               HDA_REG_RIRBCTL_OIC));

    return ZX_OK;
}

zx_status_t IntelHDAController::InitInternal(zx_device_t* pci_dev) {
    default_domain_ = dispatcher::ExecutionDomain::Create();
    if (default_domain_ == nullptr)
        return ZX_ERR_NO_MEMORY;

    zx_status_t res;
    res = SetupPCIDevice(pci_dev);
    if (res != ZX_OK)
        return res;

    // Check our hardware version
    uint8_t major, minor;
    major = REG_RD(&regs_->vmaj);
    minor = REG_RD(&regs_->vmin);

    if ((1 != major) || (0 != minor)) {
        LOG("Unexpected HW revision %d.%d!\n", major, minor);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Completely reset the hardware
    res = ResetControllerHW();
    if (res != ZX_OK)
        return res;

    // Setup interrupts and enable bus mastering.
    res = SetupPCIInterrupts();
    if (res != ZX_OK)
        return res;

    // Allocate and set up our stream descriptors.
    res = SetupStreamDescriptors();
    if (res != ZX_OK)
        return res;

    // Allocate and set up the codec communication ring buffers (CORB/RIRB)
    res = SetupCommandBuffer();
    if (res != ZX_OK)
        return res;

    // Start the IRQ thread.
    // TODO(johngro) : Fix this; C11 does not support thrd_create_with_name but MUSL does.
    int c11_res;
#if 0
    c11_res = thrd_create_with_name(
            &irq_thread_,
            [](void* ctx) -> int { return static_cast<IntelHDAController*>(ctx)->IRQThread(); },
            this,
            dev_name());
#else
    c11_res = thrd_create(
            &irq_thread_,
            [](void* ctx) -> int { return static_cast<IntelHDAController*>(ctx)->IRQThread(); },
            this);
#endif

    if (c11_res < 0) {
        LOG("Failed create IRQ thread! (res = %d)\n", c11_res);
        SetState(State::SHUT_DOWN);
        return ZX_ERR_INTERNAL;
    }

    irq_thread_started_ = true;

    // Publish our device.  If something goes wrong, shut down our IRQ thread
    // immediately.  Otherwise, transition to the OPERATING state and signal the
    // IRQ thread so it can begin to look for (and publish) codecs.
    //
    // TODO(johngro): We are making an assumption here about the threading
    // behavior of the device driver framework.  In particular, we are assuming
    // that Unbind will never be called after the device has been published, but
    // before Bind has unbound all the way up to the framework.  If this *can*
    // happen, then we have a race condition which would proceed as follows.
    //
    // 1) Device is published (device_add below)
    // 2) Before SetState (below) Unbind is called, which triggers a transition
    //    to SHUTTING_DOWN and wakes up the IRQ thread..
    // 3) Before the IRQ thread wakes up and exits, the SetState (below)
    //    transitions to OPERATING.
    // 4) The IRQ thread is now operating, but should be shut down.
    //
    // At some point, we need to verify the threading assumptions being made
    // here.  If they are not valid, this needs to be revisited and hardened.

    // Put an unmanaged reference to ourselves in the device node we are about
    // to publish.  Only perform an manual AddRef if we succeed in publishing
    // our device.

    // Generate a device name and initialize our device structure
    snprintf(debug_tag_, sizeof(debug_tag_), "intel-hda-%03u", id());

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = debug_tag_;
    args.ctx = this;
    args.ops = &CONTROLLER_DEVICE_THUNKS;
    args.proto_id = ZX_PROTOCOL_IHDA;

    res = device_add(pci_dev_, &args, &dev_node_);
    if (res == ZX_OK) {
        this->AddRef();
        SetState(State::OPERATING);
        WakeupIRQThread();
    }

    return res;
}

zx_status_t IntelHDAController::Init(zx_device_t* pci_dev) {
    zx_status_t res = InitInternal(pci_dev);

    if (res != ZX_OK)
        DeviceShutdown();

    return res;
}

}  // namespace intel_hda
}  // namespace audio
