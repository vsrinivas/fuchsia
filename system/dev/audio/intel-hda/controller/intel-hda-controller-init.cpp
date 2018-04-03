// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/arch_ops.h>
#include <limits.h>

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
}  // anon namespace

zx_status_t IntelHDAController::ResetControllerHW() {
    zx_status_t res;

    // Are we currently being held in reset?  If not, try to make sure that all
    // of our DMA streams are stopped and have been reset (but are not being
    // held in reset) before cycling the controller.  Anecdotally, holding a
    // stream in reset while attempting to reset the controller on some Skylake
    // hardware has caused some pretty profound hardware lockups which require
    // fully removing power (warm reboot == not good enough) to recover from.
    if (REG_RD(&regs()->gctl) & HDA_REG_GCTL_HWINIT) {
        // Explicitly disable all top level interrupt sources.
        REG_WR(&regs()->intsts, 0u);
        hw_mb();

        // Count the number of streams present in the hardware and
        // unconditionally stop and reset all of them.
        uint16_t gcap = REG_RD(&regs()->gcap);
        unsigned int total_stream_cnt = HDA_REG_GCAP_ISS(gcap)
                                      + HDA_REG_GCAP_OSS(gcap)
                                      + HDA_REG_GCAP_BSS(gcap);

        if (total_stream_cnt > countof(regs()->stream_desc)) {
            LOG(ERROR,
                "Fatal error during reset!  Controller reports more streams (%u) "
                "than should be possible for IHDA hardware.  (GCAP = 0x%04hx)\n",
                total_stream_cnt, gcap);
            return ZX_ERR_INTERNAL;
        }

        hda_stream_desc_regs_t* sregs = regs()->stream_desc;
        for (uint32_t i = 0; i < total_stream_cnt; ++i) {
            IntelHDAStream::Reset(sregs + i);
        }

        // Explicitly shut down any CORB/RIRB DMA
        REG_WR(&regs()->corbctl, 0u);
        REG_WR(&regs()->rirbctl, 0u);
    }

    // Assert the reset signal and wait for the controller to ack.
    REG_CLR_BITS(&regs()->gctl, HDA_REG_GCTL_HWINIT);
    hw_mb();

    res = WaitCondition(INTEL_HDA_RESET_TIMEOUT_NSEC,
                        INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                        [](void* r) -> bool {
                           auto regs = reinterpret_cast<hda_registers_t*>(r);
                           return (REG_RD(&regs->gctl) & HDA_REG_GCTL_HWINIT) == 0;
                        },
                        regs());

    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to enter reset! (res %d)\n", res);
        return res;
    }

    // Wait the spec mandated hold time.
    zx_nanosleep(zx_deadline_after(INTEL_HDA_RESET_HOLD_TIME_NSEC));

    // Deassert the reset signal and wait for the controller to ack.
    REG_SET_BITS<uint32_t>(&regs()->gctl, HDA_REG_GCTL_HWINIT);
    hw_mb();

    res = WaitCondition(INTEL_HDA_RESET_TIMEOUT_NSEC,
                        INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                        [](void* r) -> bool {
                           auto regs = reinterpret_cast<hda_registers_t*>(r);
                           return (REG_RD(&regs->gctl) & HDA_REG_GCTL_HWINIT) != 0;
                        },
                        regs());

    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to leave reset! (res %d)\n", res);
        return res;
    }

    // Wait the spec mandated discovery time.
    zx_nanosleep(zx_deadline_after(INTEL_HDA_CODEC_DISCOVERY_WAIT_NSEC));
    return res;
}

zx_status_t IntelHDAController::ResetCORBRdPtrLocked() {
    zx_status_t res;

    /* Set the reset bit, then wait for ack from the HW.  See Section 3.3.21 */
    REG_WR(&regs()->corbrp, HDA_REG_CORBRP_RST);
    hw_mb();

    if ((res = WaitCondition(INTEL_HDA_RING_BUF_RESET_TIMEOUT_NSEC,
                             INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                             [](void* r) -> bool {
                                auto regs = reinterpret_cast<hda_registers_t*>(r);
                                return (REG_RD(&regs->corbrp) & HDA_REG_CORBRP_RST) != 0;
                             },
                             regs())) != ZX_OK) {
        return res;
    }

    /* Clear the reset bit, then wait for ack */
    REG_WR(&regs()->corbrp, 0u);
    hw_mb();

    if ((res = WaitCondition(INTEL_HDA_RING_BUF_RESET_TIMEOUT_NSEC,
                             INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                             [](void* r) -> bool {
                                auto regs = reinterpret_cast<hda_registers_t*>(r);
                                return (REG_RD(&regs->corbrp) & HDA_REG_CORBRP_RST) == 0;
                             },
                             regs())) != ZX_OK) {
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
        LOG(ERROR, "Device already initialized!\n");
        return ZX_ERR_BAD_STATE;
    }

    ZX_DEBUG_ASSERT(!irq_.is_valid());
    ZX_DEBUG_ASSERT(mapped_regs_.start() == nullptr);
    ZX_DEBUG_ASSERT(pci_.ops == nullptr);

    pci_dev_ = pci_dev;

    // The device had better be a PCI device, or we are very confused.
    res = device_get_protocol(pci_dev_, ZX_PROTOCOL_PCI, reinterpret_cast<void*>(&pci_));
    if (res != ZX_OK) {
        LOG(ERROR, "PCI device does not support PCI protocol! (res %d)\n", res);
        return res;
    }

    // Fetch our device info and use it to re-generate our debug tag once we
    // know our BDF address.
    ZX_DEBUG_ASSERT(pci_.ops != nullptr);
    res = pci_get_device_info(&pci_, &pci_dev_info_);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to fetch basic PCI device info! (res %d)\n", res);
        return res;
    }

    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA Controller %02x:%02x.%01x",
             pci_dev_info_.bus_id,
             pci_dev_info_.dev_id,
             pci_dev_info_.func_id);

    // Fetch a handle to our bus transaction initiator and stash it in a ref
    // counted object (so we can manage the lifecycle as we share the handle
    // with various pinned VMOs we need to grant the controller BTI access to).
    zx::bti pci_bti;
    res = pci_get_bti(&pci_, 0, pci_bti.reset_and_get_address());
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to get BTI handle for IHDA Controller (res %d)\n", res);
        return res;
    }

    pci_bti_ = RefCountedBti::Create(fbl::move(pci_bti));
    if (pci_bti_ == nullptr) {
        LOG(ERROR, "Out of memory while attempting to allocate BTI wrapper for IHDA Controller\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Fetch the BAR which holds our main registers, then sanity check the type
    // and size.
    zx_pci_bar_t bar_info;
    res = pci_get_bar(&pci_, 0u, &bar_info);
    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to fetch registers from PCI (res %d)\n", res);
        return res;
    }

    if (bar_info.type != PCI_BAR_TYPE_MMIO) {
        LOG(ERROR, "Bad register window type (expected %u got %u)\n",
                PCI_BAR_TYPE_MMIO, bar_info.type);
        return ZX_ERR_INTERNAL;
    }

    // We should have a valid handle now, make sure we don't leak it.
    zx::vmo bar_vmo(bar_info.handle);
    if (bar_info.size != sizeof(hda_all_registers_t)) {
        LOG(ERROR, "Bad register window size (expected 0x%zx got 0x%zx)\n",
            sizeof(hda_all_registers_t), bar_info.size);
        return ZX_ERR_INTERNAL;
    }

    // Since this VMO provides access to our registers, make sure to set the
    // cache policy to UNCACHED_DEVICE
    res = bar_vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to set cache policy for PCI registers (res %d)\n", res);
        return res;
    }

    // Map the VMO in, make sure to put it in the same VMAR as the rest of our
    // registers.
    constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    res = mapped_regs_.Map(bar_vmo, 0, bar_info.size, CPU_MAP_FLAGS, DriverVmars::registers());
    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to map registers (res %d)\n", res);
        return res;
    }

    return ZX_OK;
}

zx_status_t IntelHDAController::SetupPCIInterrupts() {
    ZX_DEBUG_ASSERT(pci_dev_ != nullptr);

    // Configure our IRQ mode and map our IRQ handle.  Try to use MSI, but if
    // that fails, fall back on legacy IRQs.
    zx_status_t res = pci_set_irq_mode(&pci_, ZX_PCIE_IRQ_MODE_MSI, 1);
    if (res != ZX_OK) {
        res = pci_set_irq_mode(&pci_, ZX_PCIE_IRQ_MODE_LEGACY, 1);
        if (res != ZX_OK) {
            LOG(ERROR, "Failed to set IRQ mode (%d)!\n", res);
            return res;
        } else {
            LOG(ERROR, "Falling back on legacy IRQ mode!\n");
        }
    }

    ZX_DEBUG_ASSERT(!irq_.is_valid());
    res = pci_map_interrupt(&pci_, 0, irq_.reset_and_get_address());
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to map IRQ! (res %d)\n", res);
        return res;
    }

    // Enable Bus Mastering so we can DMA data and receive MSIs
    res = pci_enable_bus_master(&pci_, true);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to enable PCI bus mastering!\n");
        return res;
    }

    return ZX_OK;
}

zx_status_t IntelHDAController::SetupStreamDescriptors() {
    fbl::AutoLock stream_pool_lock(&stream_pool_lock_);

    // Sanity check our stream counts.
    uint16_t gcap;
    unsigned int input_stream_cnt, output_stream_cnt, bidir_stream_cnt, total_stream_cnt;
    gcap              = REG_RD(&regs()->gcap);
    input_stream_cnt  = HDA_REG_GCAP_ISS(gcap);
    output_stream_cnt = HDA_REG_GCAP_OSS(gcap);
    bidir_stream_cnt  = HDA_REG_GCAP_BSS(gcap);
    total_stream_cnt  = input_stream_cnt + output_stream_cnt + bidir_stream_cnt;

    static_assert(MAX_STREAMS_PER_CONTROLLER == countof(regs()->stream_desc),
                  "Max stream count mismatch!");

    if (!total_stream_cnt || (total_stream_cnt > countof(regs()->stream_desc))) {
        LOG(ERROR, "Invalid stream counts in GCAP register (In %u Out %u Bidir %u; Max %zu)\n",
            input_stream_cnt, output_stream_cnt, bidir_stream_cnt, countof(regs()->stream_desc));
        return ZX_ERR_INTERNAL;
    }

    // Allocate our stream descriptors and populate our free lists.
    for (uint32_t i = 0; i < total_stream_cnt; ++i) {
        uint16_t stream_id = static_cast<uint16_t>(i + 1);
        auto type = (i < input_stream_cnt)
                  ? IntelHDAStream::Type::INPUT
                  : ((i < input_stream_cnt + output_stream_cnt)
                  ? IntelHDAStream::Type::OUTPUT
                  : IntelHDAStream::Type::BIDIR);

        auto stream = IntelHDAStream::Create(type, stream_id, &regs()->stream_desc[i], pci_bti_);
        if (stream == nullptr) {
            LOG(ERROR, "Failed to create HDA stream context %u/%u\n", i, total_stream_cnt);
            return ZX_ERR_NO_MEMORY;
        }

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
        LOG(ERROR, "Invalid ring buffer capabilities! (0x%02x)\n", tmp);
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
    zx::vmo cmd_buf_vmo;
    constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    static_assert(PAGE_SIZE >= (HDA_CORB_MAX_BYTES + HDA_RIRB_MAX_BYTES),
                  "PAGE_SIZE to small to hold CORB and RIRB buffers!");
    res = cmd_buf_cpu_mem_.CreateAndMap(PAGE_SIZE,
                                        CPU_MAP_FLAGS,
                                        DriverVmars::registers(),
                                        &cmd_buf_vmo,
                                        ZX_RIGHT_SAME_RIGHTS,
                                        ZX_CACHE_POLICY_UNCACHED_DEVICE);

    if (res != ZX_OK) {
        LOG(ERROR, "Failed to create and map %u bytes for CORB/RIRB command buffers! (res %d)\n",
            PAGE_SIZE, res);
        return res;
    }

    // Pin this VMO and grant the controller access to it.  The controller will
    // need read/write access as this page contains both the command and
    // response buffers.
    //
    // TODO(johngro): If we (someday) decide that we need more isolation, we
    // should split this allocation so that there is a dedicated page for the
    // command buffer separate from the response buffer.  The controller should
    // never have a reason it needs to write to the command buffer, but it would
    // need its own page if we wanted to control the access at an IOMMU level.
    constexpr uint32_t HDA_MAP_FLAGS = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE;
    res = cmd_buf_hda_mem_.Pin(cmd_buf_vmo, pci_bti_->initiator(), HDA_MAP_FLAGS);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to pin pages for CORB/RIRB command buffers! (res %d)\n", res);
        return res;
    }

    // Start by making sure that the output and response ring buffers are being
    // held in the stopped state
    REG_WR(&regs()->corbctl, 0u);
    REG_WR(&regs()->rirbctl, 0u);

    // Reset the read and write pointers for both ring buffers
    REG_WR(&regs()->corbwp, 0u);
    res = ResetCORBRdPtrLocked();
    if (res != ZX_OK)
        return res;

    // Note; the HW does not expose a Response Input Ring Buffer Read Pointer,
    // we have to maintain our own.
    rirb_rd_ptr_ = 0;
    REG_WR(&regs()->rirbwp, HDA_REG_RIRBWP_RST);

    // Physical memory for the CORB/RIRB should already have been allocated at
    // this point
    ZX_DEBUG_ASSERT(cmd_buf_cpu_mem_.start() != 0);

    // Determine the ring buffer sizes.  If there are options, make them as
    // large as possible.
    res = SetupCommandBufferSize(&regs()->corbsize, &corb_entry_count_);
    if (res != ZX_OK)
        return res;

    res = SetupCommandBufferSize(&regs()->rirbsize, &rirb_entry_count_);
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
    const auto& region = cmd_buf_hda_mem_.region(0);
    uint64_t cmd_buf_paddr64 = static_cast<uint64_t>(region.phys_addr);

    // TODO(johngro) : If the controller does not support 64 bit phys
    // addressing, we need to make sure to get a page from low memory to use for
    // our command buffers.
    bool gcap_64bit_ok = HDA_REG_GCAP_64OK(REG_RD(&regs()->gcap));
    if ((cmd_buf_paddr64 >> 32) && !gcap_64bit_ok) {
        LOG(ERROR, "Intel HDA controller does not support 64-bit physical addressing!\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Section 4.4.1.1; corb ring buffer base address must be 128 byte aligned.
    ZX_DEBUG_ASSERT(!(cmd_buf_paddr64 & 0x7F));
    auto cmd_buf_start = reinterpret_cast<uint8_t*>(cmd_buf_cpu_mem_.start());
    REG_WR(&regs()->corblbase, ((uint32_t)(cmd_buf_paddr64 & 0xFFFFFFFF)));
    REG_WR(&regs()->corbubase, ((uint32_t)(cmd_buf_paddr64 >> 32)));
    corb_ = reinterpret_cast<CodecCommand*>(cmd_buf_start);

    cmd_buf_paddr64 += HDA_CORB_MAX_BYTES;

    // Section 4.4.2.2; rirb ring buffer base address must be 128 byte aligned.
    ZX_DEBUG_ASSERT(!(cmd_buf_paddr64 & 0x7F));
    REG_WR(&regs()->rirblbase, ((uint32_t)(cmd_buf_paddr64 & 0xFFFFFFFF)));
    REG_WR(&regs()->rirbubase, ((uint32_t)(cmd_buf_paddr64 >> 32)));
    rirb_ = reinterpret_cast<CodecResponse*>(cmd_buf_start + HDA_CORB_MAX_BYTES);

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
    REG_WR(&regs()->rintcnt, thresh);

    // Clear out any lingering interrupt status
    REG_WR(&regs()->corbsts, HDA_REG_CORBSTS_MEI);
    REG_WR(&regs()->rirbsts, OR(HDA_REG_RIRBSTS_INTFL, HDA_REG_RIRBSTS_OIS));

    // Enable the TX/RX IRQs and DMA engines.
    REG_WR(&regs()->corbctl, OR(HDA_REG_CORBCTL_MEIE, HDA_REG_CORBCTL_DMA_EN));
    REG_WR(&regs()->rirbctl, OR(OR(HDA_REG_RIRBCTL_INTCTL, HDA_REG_RIRBCTL_DMA_EN),
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
    major = REG_RD(&regs()->vmaj);
    minor = REG_RD(&regs()->vmin);

    if ((1 != major) || (0 != minor)) {
        LOG(ERROR, "Unexpected HW revision %d.%d!\n", major, minor);
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
        LOG(ERROR, "Failed create IRQ thread! (res = %d)\n", c11_res);
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
    char dev_name[ZX_DEVICE_NAME_MAX] = { 0 };
    snprintf(dev_name, sizeof(dev_name), "intel-hda-%03u", id());

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = dev_name;
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
