// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/pci.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <limits.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <utility>

#include <hw/arch_ops.h>
#include <intel-hda/utils/status.h>

#include "debug-logging.h"
#include "device-ids.h"
#include "intel-dsp.h"
#include "intel-hda-controller.h"
#include "intel-hda-stream.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

namespace {
constexpr zx_duration_t INTEL_HDA_RESET_HOLD_TIME_NSEC = ZX_USEC(100);       // Section 5.5.1.2
constexpr zx_duration_t INTEL_HDA_RESET_TIMEOUT_NSEC = ZX_SEC(2);            // Arbitrary
constexpr zx_duration_t INTEL_HDA_RING_BUF_RESET_TIMEOUT_NSEC = ZX_SEC(2);   // Arbitrary
constexpr zx_duration_t INTEL_HDA_RESET_POLL_TIMEOUT_NSEC = ZX_USEC(10);     // Arbitrary
constexpr zx_duration_t INTEL_HDA_CODEC_DISCOVERY_WAIT_NSEC = ZX_USEC(521);  // Section 4.3

constexpr unsigned int MAX_CAPS = 10;  // Arbitrary number of capabilities to check
}  // namespace

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
    REG_WR(&regs()->intctl, 0u);
    hw_mb();

    // Count the number of streams present in the hardware and
    // unconditionally stop and reset all of them.
    uint16_t gcap = REG_RD(&regs()->gcap);
    unsigned int total_stream_cnt =
        HDA_REG_GCAP_ISS(gcap) + HDA_REG_GCAP_OSS(gcap) + HDA_REG_GCAP_BSS(gcap);

    if (total_stream_cnt > std::size(regs()->stream_desc)) {
      LOG(ERROR,
          "Fatal error during reset!  Controller reports more streams (%u) "
          "than should be possible for IHDA hardware.  (GCAP = 0x%04hx)\n",
          total_stream_cnt, gcap);
      return ZX_ERR_INTERNAL;
    }

    MMIO_PTR hda_stream_desc_regs_t* sregs = regs()->stream_desc;
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

  res = WaitCondition(
      INTEL_HDA_RESET_TIMEOUT_NSEC, INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
      [this]() -> bool { return (REG_RD(&regs()->gctl) & HDA_REG_GCTL_HWINIT) == 0; });

  if (res != ZX_OK) {
    LOG(ERROR, "Error attempting to enter reset! (res %d)\n", res);
    return res;
  }

  // Wait the spec mandated hold time.
  zx_nanosleep(zx_deadline_after(INTEL_HDA_RESET_HOLD_TIME_NSEC));

  // Deassert the reset signal and wait for the controller to ack.
  REG_SET_BITS<uint32_t>(&regs()->gctl, HDA_REG_GCTL_HWINIT);
  hw_mb();

  res = WaitCondition(
      INTEL_HDA_RESET_TIMEOUT_NSEC, INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
      [this]() -> bool { return (REG_RD(&regs()->gctl) & HDA_REG_GCTL_HWINIT) != 0; });

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

  if ((res = WaitCondition(INTEL_HDA_RING_BUF_RESET_TIMEOUT_NSEC, INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                           [this]() -> bool {
                             return (REG_RD(&regs()->corbrp) & HDA_REG_CORBRP_RST) != 0;
                           })) != ZX_OK) {
    return res;
  }

  /* Clear the reset bit, then wait for ack */
  REG_WR(&regs()->corbrp, 0u);
  hw_mb();

  if ((res = WaitCondition(INTEL_HDA_RING_BUF_RESET_TIMEOUT_NSEC, INTEL_HDA_RESET_POLL_TIMEOUT_NSEC,
                           [this]() -> bool {
                             return (REG_RD(&regs()->corbrp) & HDA_REG_CORBRP_RST) == 0;
                           })) != ZX_OK) {
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

  ZX_DEBUG_ASSERT(irq_ != nullptr);
  ZX_DEBUG_ASSERT(!mapped_regs_.has_value());
  ZX_DEBUG_ASSERT(pci_.ops == nullptr);

  pci_dev_ = pci_dev;

  // The device had better be a PCI device, or we are very confused.
  res = device_get_protocol(pci_dev_, ZX_PROTOCOL_PCI, reinterpret_cast<void*>(&pci_));
  if (res != ZX_OK) {
    LOG(ERROR, "PCI device does not support PCI protocol! (res %d)\n", res);
    return res;
  }

  ddk::Pci pci(pci_);

  // Fetch our device info and use it to re-generate our debug tag once we
  // know our BDF address.
  ZX_DEBUG_ASSERT(pci_.ops != nullptr);
  res = pci.GetDeviceInfo(&pci_dev_info_);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to fetch basic PCI device info! (res %d)\n", res);
    return res;
  }

  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA Controller %02x:%02x.%01x", pci_dev_info_.bus_id,
           pci_dev_info_.dev_id, pci_dev_info_.func_id);

  // Fetch a handle to our bus transaction initiator and stash it in a ref
  // counted object (so we can manage the lifecycle as we share the handle
  // with various pinned VMOs we need to grant the controller BTI access to).
  zx::bti pci_bti;
  res = pci.GetBti(0, &pci_bti);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to get BTI handle for IHDA Controller (res %d)\n", res);
    return res;
  }

  pci_bti_ = RefCountedBti::Create(std::move(pci_bti));
  if (pci_bti_ == nullptr) {
    LOG(ERROR, "Out of memory while attempting to allocate BTI wrapper for IHDA Controller\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Fetch the BAR which holds our main registers.
  std::optional<ddk::MmioBuffer> mmio;
  res = pci.MapMmio(0u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to fetch and map registers from PCI (res %d)\n", res);
    return res;
  }

  // We should have a valid handle now, make sure we don't leak it.
  if (mmio->get_size() < sizeof(hda_all_registers_t)) {
    LOG(ERROR, "Bad register window size (expected 0x%zx got 0x%zx)\n", sizeof(hda_all_registers_t),
        mmio->get_size());
    return ZX_ERR_INTERNAL;
  }

  mapped_regs_ = std::move(mmio);

  return ZX_OK;
}

zx_status_t IntelHDAController::SetupPCIInterrupts() {
  ZX_DEBUG_ASSERT(pci_dev_ != nullptr);

  // Make absolutely sure that IRQs are disabled at the controller level
  // before proceeding.
  REG_WR(&regs()->intctl, 0u);

  // Configure our IRQ mode and map our IRQ handle.
  zx_status_t res = pci_configure_irq_mode(&pci_, 1);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to set IRQ mode (%d)!\n", res);
    return res;
  }

  // Retrieve our PCI interrupt, then use it to activate our IRQ dispatcher.
  zx::interrupt irq;
  res = pci_map_interrupt(&pci_, 0, irq.reset_and_get_address());
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to map IRQ! (res %d)\n", res);
    return res;
  }

  ZX_DEBUG_ASSERT(irq_ != nullptr);
  auto irq_handler = [controller = fbl::RefPtr(this)](const dispatcher::Interrupt* irq,
                                                      zx_time_t timestamp) -> zx_status_t {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(t, controller->default_domain_);
    LOG_EX(TRACE, *controller, "Hard IRQ (ts = %lu)\n", timestamp);
    return controller->HandleIrq();
  };

  res = irq_->Activate(default_domain_, std::move(irq), std::move(irq_handler));

  if (res != ZX_OK) {
    LOG(ERROR, "Failed to activate IRQ dispatcher! (res %d)\n", res);
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
  gcap = REG_RD(&regs()->gcap);
  input_stream_cnt = HDA_REG_GCAP_ISS(gcap);
  output_stream_cnt = HDA_REG_GCAP_OSS(gcap);
  bidir_stream_cnt = HDA_REG_GCAP_BSS(gcap);
  total_stream_cnt = input_stream_cnt + output_stream_cnt + bidir_stream_cnt;

  using RegType = decltype(IntelHDAController::regs());
  static_assert(MAX_STREAMS_PER_CONTROLLER == countof(RegType()->stream_desc),
                "Max stream count mismatch!");

  if (!total_stream_cnt || (total_stream_cnt > countof(RegType()->stream_desc))) {
    LOG(ERROR, "Invalid stream counts in GCAP register (In %u Out %u Bidir %u; Max %zu)\n",
        input_stream_cnt, output_stream_cnt, bidir_stream_cnt, countof(RegType()->stream_desc));
    return ZX_ERR_INTERNAL;
  }

  // Allocate our stream descriptors and populate our free lists.
  for (uint32_t i = 0; i < total_stream_cnt; ++i) {
    uint16_t stream_id = static_cast<uint16_t>(i + 1);
    auto type = (i < input_stream_cnt)
                    ? IntelHDAStream::Type::INPUT
                    : ((i < input_stream_cnt + output_stream_cnt) ? IntelHDAStream::Type::OUTPUT
                                                                  : IntelHDAStream::Type::BIDIR);

    auto stream =
        IntelHDAStream::Create(type, stream_id, &regs()->stream_desc[i], pci_bti_, vmar_manager_);
    if (stream == nullptr) {
      LOG(ERROR, "Failed to create HDA stream context %u/%u\n", i, total_stream_cnt);
      return ZX_ERR_NO_MEMORY;
    }

    ZX_DEBUG_ASSERT(i < std::size(all_streams_));
    ZX_DEBUG_ASSERT(all_streams_[i] == nullptr);
    all_streams_[i] = stream;
    ReturnStreamLocked(std::move(stream));
  }

  return ZX_OK;
}

zx_status_t IntelHDAController::SetupCommandBufferSize(MMIO_PTR uint8_t* size_reg,
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
  constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  static_assert(PAGE_SIZE >= (HDA_CORB_MAX_BYTES + HDA_RIRB_MAX_BYTES),
                "PAGE_SIZE to small to hold CORB and RIRB buffers!");
  res = cmd_buf_cpu_mem_.CreateAndMap(PAGE_SIZE, CPU_MAP_FLAGS, vmar_manager_, &cmd_buf_vmo,
                                      ZX_RIGHT_SAME_RIGHTS, ZX_CACHE_POLICY_UNCACHED_DEVICE);

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
  ZX_DEBUG_ASSERT(cmd_buf_cpu_mem_.start() != nullptr);

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
  corb_max_in_flight_ =
      rirb_mask_ > RIRB_RESERVED_RESPONSE_SLOTS ? rirb_mask_ - RIRB_RESERVED_RESPONSE_SLOTS : 1;
  corb_max_in_flight_ = std::min(corb_max_in_flight_, corb_mask_);

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
  REG_WR(&regs()->corblbase, (static_cast<uint32_t>(cmd_buf_paddr64 & 0xFFFFFFFF)));
  REG_WR(&regs()->corbubase, (static_cast<uint32_t>(cmd_buf_paddr64 >> 32)));
  corb_ = reinterpret_cast<CodecCommand*>(cmd_buf_start);

  cmd_buf_paddr64 += HDA_CORB_MAX_BYTES;

  // Section 4.4.2.2; rirb ring buffer base address must be 128 byte aligned.
  ZX_DEBUG_ASSERT(!(cmd_buf_paddr64 & 0x7F));
  REG_WR(&regs()->rirblbase, (static_cast<uint32_t>(cmd_buf_paddr64 & 0xFFFFFFFF)));
  REG_WR(&regs()->rirbubase, (static_cast<uint32_t>(cmd_buf_paddr64 >> 32)));
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
  REG_WR(&regs()->rirbctl,
         OR(OR(HDA_REG_RIRBCTL_INTCTL, HDA_REG_RIRBCTL_DMA_EN), HDA_REG_RIRBCTL_OIC));

  return ZX_OK;
}

zx_status_t IntelHDAController::ProbeAudioDSP(zx_device_t* dsp_dev) {
  // This driver only supports the Audio DSP on Kabylake.
  if ((pci_dev_info_.vendor_id != INTEL_HDA_PCI_VID) ||
      (pci_dev_info_.device_id != INTEL_HDA_PCI_DID_KABYLAKE)) {
    LOG(DEBUG, "Audio DSP is not supported for device 0x%04x:0x%04x\n", pci_dev_info_.vendor_id,
        pci_dev_info_.device_id);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Look for the processing pipe capability structure. Existence of this
  // structure means the Audio DSP is supported by the HW.
  uint32_t offset = REG_RD(&regs()->llch);
  if ((offset == 0) || (offset >= mapped_regs_->get_size())) {
    LOG(DEBUG, "Invalid LLCH offset to capability structures: 0x%08x\n", offset);
    return ZX_ERR_INTERNAL;
  }

  MMIO_PTR hda_pp_registers_t* pp_regs = nullptr;
  MMIO_PTR hda_pp_registers_t* found_regs = nullptr;
  MMIO_PTR uint8_t* regs_ptr = nullptr;
  unsigned int count = 0;
  uint32_t cap;
  do {
    regs_ptr = reinterpret_cast<MMIO_PTR uint8_t*>(regs()) + offset;
    pp_regs = reinterpret_cast<MMIO_PTR hda_pp_registers_t*>(regs_ptr);
    cap = REG_RD(&pp_regs->ppch);
    if ((cap & HDA_CAP_ID_MASK) == HDA_CAP_PP_ID) {
      found_regs = pp_regs;
      break;
    }
    offset = cap & HDA_CAP_PTR_MASK;
    count += 1;
  } while ((count < MAX_CAPS) && (offset != 0));

  if (found_regs == nullptr) {
    LOG(DEBUG, "Pipe processing capability structure not found\n");
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  dsp_ = fbl::AdoptRef(new (&ac) IntelDsp(this, pp_regs));
  if (!ac.check()) {
    LOG(ERROR, "Out of memory attempting to allocate DSP device\n");
    return ZX_ERR_NO_MEMORY;
  }

  Status result = dsp_->Init(dsp_dev);
  if (!result.ok()) {
    LOG(ERROR, "Failed to initialize DSP device: %s\n", result.ToString().c_str());
    return result.code();
  }

  return ZX_OK;
}

zx_status_t IntelHDAController::InitInternal(zx_device_t* pci_dev) {
  // TODO(johngro): see fxbug.dev/30888; remove this priority boost when we can, and
  // when there is a better way of handling real time requirements.
  //
  // Right now, the interrupt handler runs in the same execution domain as all
  // of the other event sources managed by the HDA controller.  If it is
  // configured to run and send DMA ring buffer notifications to the higher
  // level, the IRQ needs to be running at a boosted priority in order to have
  // a chance of meeting its real time deadlines.
  //
  // There is currently no terribly good way to control this dynamically, or
  // to apply this priority only to the interrupt event source and not others.
  // If it ever becomes a serious issue that the channel event handlers in
  // this system are running at boosted priority, we can come back here and
  // split the IRQ handler to run its own dedicated exeuction domain instead
  // of using the default domain.

  zx_handle_t profile;
  zx_status_t res = device_get_profile(pci_dev, 24 /* HIGH_PRIORITY */,
                                       "src/media/audio/drivers/intel-hda/controller", &profile);
  if (res != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }

  default_domain_ = dispatcher::ExecutionDomain::Create(zx::profile(profile));
  if (default_domain_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  irq_ = dispatcher::Interrupt::Create();
  if (irq_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  irq_wakeup_event_ = dispatcher::WakeupEvent::Create();
  if (irq_wakeup_event_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  res = irq_wakeup_event_->Activate(
      default_domain_,
      [controller = fbl::RefPtr(this)](const dispatcher::WakeupEvent* evt) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, controller->default_domain_);
        LOG_EX(TRACE, *controller, "SW IRQ Wakeup\n");
        return controller->HandleIrq();
      });
  if (res != ZX_OK) {
    return res;
  }

  res = SetupPCIDevice(pci_dev);
  if (res != ZX_OK) {
    return res;
  }

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

  // Generate a device name, initialize our device structure, and attempt to
  // publish our device.
  char dev_name[ZX_DEVICE_NAME_MAX] = {0};
  snprintf(dev_name, sizeof(dev_name), "intel-hda-%03u", id());

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = dev_name;
  args.ctx = this;
  args.ops = &CONTROLLER_DEVICE_THUNKS;
  args.proto_id = ZX_PROTOCOL_IHDA;

  // Manually add a reference to this object.  If we succeeded in publishing,
  // the DDK will be holding an unmanaged reference to us in our device's ctx
  // pointer.  We will re-claim the reference when the DDK eventually calls
  // our Release hook.
  this->AddRef();

  res = device_add(pci_dev_, &args, &dev_node_);
  if (res != ZX_OK) {
    // We failed to publish our device.  Release the manual reference we
    // just added.
    __UNUSED bool should_destruct;
    should_destruct = this->Release();
    ZX_DEBUG_ASSERT(!should_destruct);
    return res;
  }

  // Flag the fact that we have entered the operating state.
  SetState(State::OPERATING);

  // Make sure that interrupts are completely disabled before proceeding.
  // If we have a unmasked, pending IRQ, we need to make sure that it
  // generates and interrupt once we have finished this interrupt
  // configuration.
  REG_WR(&regs()->intctl, 0u);

  // Clear our STATESTS shadow, setup the WAKEEN register to wake us
  // up if there is any change to the codec enumeration status.  This will
  // kick off the process of codec enumeration.
  REG_SET_BITS(&regs()->wakeen, HDA_REG_STATESTS_MASK);

  // Allow unsolicited codec responses
  REG_SET_BITS(&regs()->gctl, HDA_REG_GCTL_UNSOL);

  // Compute the set of interrupts we may be interested in during
  // operation, then enable those interrupts.
  uint32_t interesting_irqs = HDA_REG_INTCTL_GIE | HDA_REG_INTCTL_CIE;
  for (uint32_t i = 0; i < countof(all_streams_); ++i) {
    if (all_streams_[i] != nullptr)
      interesting_irqs |= HDA_REG_INTCTL_SIE(i);
  }
  REG_WR(&regs()->intctl, interesting_irqs);

  // Probe for the Audio DSP. This is done after adding the HDA controller
  // device because the Audio DSP will be added a child to the HDA
  // controller and ddktl requires the parent device node to be initialized
  // at construction time.
  zx_status_t dsp_probe_result = ProbeAudioDSP(dev_node_);
  if (dsp_probe_result != ZX_OK) {
    LOG(WARNING, "Error probing DSP: %s", zx_status_get_string(dsp_probe_result));
    // We continue despite the failure because the absence of the Audio
    // DSP is not (always) a failure.
    // TODO(yky) Come up with a way to warn for the absence of Audio DSP
    // on platforms that require it.
  }

  return ZX_OK;
}

zx_status_t IntelHDAController::Init(zx_device_t* pci_dev) {
  vmar_manager_ = CreateDriverVmars();
  if (vmar_manager_.get() == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t res = InitInternal(pci_dev);
  if (res != ZX_OK) {
    DeviceShutdown();
    return res;
  }

  return ZX_OK;
}

}  // namespace intel_hda
}  // namespace audio
