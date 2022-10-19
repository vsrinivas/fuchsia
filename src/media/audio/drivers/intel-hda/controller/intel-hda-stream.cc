// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-hda-stream.h"

#include <lib/ddk/hw/arch_ops.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/zx/clock.h>
#include <string.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <limits>
#include <utility>

#include <intel-hda/utils/utils.h>

#include "debug-logging.h"
#include "hda-codec-connection.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

namespace {
// Note: these timeouts are arbitrary; the spec provides no guidance here.
// That said, it is hard to imagine it taking more than a single audio
// frame's worth of time, so 10mSec should be more then generous enough.
constexpr zx_time_t IHDA_SD_MAX_RESET_TIME_NSEC = 10000000u;  // 10mSec
constexpr zx_time_t IHDA_SD_RESET_POLL_TIME_NSEC = 100000u;   // 100uSec
constexpr zx_time_t IHDA_SD_STOP_HOLD_TIME_NSEC = 100000u;
constexpr uint32_t DMA_ALIGN = 128;
constexpr uint32_t DMA_ALIGN_MASK = DMA_ALIGN - 1;
namespace audio_fidl = fuchsia_hardware_audio;
}  // namespace

fbl::RefPtr<IntelHDAStream> IntelHDAStream::Create(Type type, uint16_t id,
                                                   MMIO_PTR hda_stream_desc_regs_t* regs,
                                                   const fbl::RefPtr<RefCountedBti>& pci_bti,
                                                   fbl::RefPtr<fzl::VmarManager> vmar_manager) {
  fbl::AllocChecker ac;
  auto ret = fbl::AdoptRef(new (&ac) IntelHDAStream(type, id, regs, pci_bti, vmar_manager));
  if (!ac.check()) {
    return nullptr;
  }

  zx_status_t res = ret->Initialize();
  if (res != ZX_OK) {
    // Initialize should have already logged the warning with the proper
    // debug prefix for the stream.  Don't bother to do so here.
    return nullptr;
  }

  return ret;
}

IntelHDAStream::IntelHDAStream(Type type, uint16_t id, MMIO_PTR hda_stream_desc_regs_t* regs,
                               const fbl::RefPtr<RefCountedBti>& pci_bti,
                               fbl::RefPtr<fzl::VmarManager> vmar_manager)
    : type_(type), id_(id), regs_(regs), vmar_manager_(std::move(vmar_manager)), pci_bti_(pci_bti) {
  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA_SD #%u", id_);
}

IntelHDAStream::~IntelHDAStream() { ZX_DEBUG_ASSERT(!running_); }

zx_status_t IntelHDAStream::Initialize() {
  // BDL entries should be 16 bytes long, meaning that we should be able to
  // fit 256 of them perfectly into a single 4k page.
  constexpr size_t MAX_BDL_BYTES = sizeof(IntelHDABDLEntry) * MAX_BDL_LENGTH;
  ZX_ASSERT_MSG(MAX_BDL_BYTES <= zx_system_get_page_size(),
                "A max length BDL must fit inside a single page!");

  // Create a VMO made of a single page and map it for read/write so the CPU
  // has access to it.
  constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  zx::vmo bdl_vmo;
  zx_status_t res;
  res = bdl_cpu_mem_.CreateAndMap(zx_system_get_page_size(), CPU_MAP_FLAGS, vmar_manager_, &bdl_vmo,
                                  ZX_RIGHT_SAME_RIGHTS, ZX_CACHE_POLICY_UNCACHED_DEVICE);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to create and map %u bytes for stream BDL! (res %d)",
        zx_system_get_page_size(), res);
    return res;
  }

  // Pin this VMO and grant the controller access to it.  The controller
  // should only need read access to buffer descriptor lists.
  constexpr uint32_t HDA_MAP_FLAGS = ZX_BTI_PERM_READ;
  ZX_DEBUG_ASSERT(pci_bti_ != nullptr);
  res = bdl_hda_mem_.Pin(bdl_vmo, pci_bti_->initiator(), HDA_MAP_FLAGS);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to pin pages for stream BDL! (res %d)", res);
    return res;
  }

  // Sanity checks.  At this point, everything should be allocated, mapped,
  // and should obey the alignment restrictions imposed by the HDA spec.
  ZX_DEBUG_ASSERT(bdl_cpu_mem_.start() != nullptr);
  ZX_DEBUG_ASSERT(!(reinterpret_cast<uintptr_t>(bdl_cpu_mem_.start()) & DMA_ALIGN_MASK));
  ZX_DEBUG_ASSERT(bdl_hda_mem_.region_count() == 1);
  ZX_DEBUG_ASSERT(!(bdl_hda_mem_.region(0).phys_addr & DMA_ALIGN_MASK));

  return ZX_OK;
}

void IntelHDAStream::EnsureStopped(MMIO_PTR hda_stream_desc_regs_t* regs) {
  // Stop the stream, but do not place it into reset.  Ack any lingering IRQ
  // status bits in the process.
  REG_CLR_BITS(&regs->ctl_sts.w, HDA_SD_REG_CTRL_RUN);
  hw_wmb();
  zx_nanosleep(zx_deadline_after(IHDA_SD_STOP_HOLD_TIME_NSEC));

  constexpr uint32_t SET = HDA_SD_REG_STS32_ACK;
  constexpr uint32_t CLR = HDA_SD_REG_CTRL_IOCE | HDA_SD_REG_CTRL_FEIE | HDA_SD_REG_CTRL_DEIE;
  REG_MOD(&regs->ctl_sts.w, CLR, SET);
  hw_wmb();
}

void IntelHDAStream::Reset(MMIO_PTR hda_stream_desc_regs_t* regs) {
  // Enter the reset state  To do this, we...
  // 1) Clear the RUN bit if it was set.
  // 2) Set the SRST bit to 1.
  // 3) Poll until the hardware acks by setting the SRST bit to 1.
  if (REG_RD(&regs->ctl_sts.w) & HDA_SD_REG_CTRL_RUN) {
    EnsureStopped(regs);
  }

  REG_WR(&regs->ctl_sts.w, HDA_SD_REG_CTRL_SRST);  // Set the reset bit.
  hw_mb();  // Make sure that all writes have gone through before we start to read.

  // Wait until the hardware acks the reset.
  zx_status_t res;
  res = WaitCondition(IHDA_SD_MAX_RESET_TIME_NSEC, IHDA_SD_RESET_POLL_TIME_NSEC, [&regs]() -> bool {
    auto val = REG_RD(&regs->ctl_sts.w);
    return (val & HDA_SD_REG_CTRL_SRST) != 0;
  });

  if (res != ZX_OK) {
    GLOBAL_LOG(ERROR, "Failed to place stream descriptor HW into reset! (res %d)", res);
  }

  // Leave the reset state.  To do this, we...
  // 1) Set the SRST bit to 0.
  // 2) Poll until the hardware acks by setting the SRST bit back to 0.
  REG_WR(&regs->ctl_sts.w, 0u);
  hw_mb();  // Make sure that all writes have gone through before we start to read.

  // Wait until the hardware acks the release from reset.
  res = WaitCondition(IHDA_SD_MAX_RESET_TIME_NSEC, IHDA_SD_RESET_POLL_TIME_NSEC, [&regs]() -> bool {
    auto val = REG_RD(&regs->ctl_sts.w);
    return (val & HDA_SD_REG_CTRL_SRST) == 0;
  });

  if (res != ZX_OK) {
    GLOBAL_LOG(ERROR, "Failed to release stream descriptor HW from reset! (res %d)", res);
  }
}

void IntelHDAStream::Configure(Type type, uint8_t tag) {
  if (type == Type::INVALID) {
    ZX_DEBUG_ASSERT(tag == 0);
  } else {
    ZX_DEBUG_ASSERT(type != Type::BIDIR);
    ZX_DEBUG_ASSERT((tag != 0) && (tag < 16));
  }

  configured_type_ = type;
  tag_ = tag;
}

zx_status_t IntelHDAStream::SetStreamFormat(async_dispatcher_t* dispatcher, uint16_t encoded_fmt,
                                            zx::channel server_endpoint) {
  // We are being given a new format.  Reset any client connection we may have
  // and stop the hardware.
  Deactivate();

  fbl::AutoLock channel_lock(&channel_lock_);
  channel_ = RingBufferChannel::Create();
  if (channel_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  fidl::OnUnboundFn<fidl::WireServer<audio_fidl::RingBuffer>> on_unbound =
      [this](fidl::WireServer<audio_fidl::RingBuffer>*, fidl::UnbindInfo,
             fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer>) {
        this->ProcessClientDeactivate();
      };

  fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> server(std::move(server_endpoint));
  fidl::BindServer<fidl::WireServer<audio_fidl::RingBuffer>>(dispatcher, std::move(server), this,
                                                             std::move(on_unbound));

  // Record and program the stream format, then record the fifo depth we get
  // based on this format selection.
  encoded_fmt_ = encoded_fmt;
  REG_WR(&regs_->fmt, encoded_fmt_);
  hw_mb();
  fifo_depth_ = REG_RD(&regs_->fifod);

  LOG(DEBUG, "Stream format set 0x%04hx; fifo is %hu bytes deep", encoded_fmt_, fifo_depth_);

  // Record our new client channel
  bytes_per_frame_ = StreamFormat(encoded_fmt).bytes_per_frame();
  if (StreamFormat(encoded_fmt).sample_rate() == 0) {
    LOG(ERROR, "Bad (zero) sample rate");
    return ZX_ERR_INVALID_ARGS;
  }
  if (bytes_per_frame_ == 0) {
    LOG(ERROR, "Bad (zero) bytes per frame");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t fifo_depth_frames = (fifo_depth_ + bytes_per_frame_ - 1) / bytes_per_frame_;
  internal_delay_nsec_ = static_cast<uint64_t>(fifo_depth_frames) * 1'000'000'000 /
                         static_cast<uint64_t>(StreamFormat(encoded_fmt).sample_rate());

  return ZX_OK;
}

void IntelHDAStream::Deactivate() {
  fbl::AutoLock channel_lock(&channel_lock_);
  DeactivateLocked();
}

void IntelHDAStream::ProcessClientDeactivate() {
  LOG(DEBUG, "Client closed channel to stream");
  fbl::AutoLock channel_lock(&channel_lock_);
  DeactivateLocked();
}

void IntelHDAStream::ProcessStreamIRQ() {
  // Regardless of whether we are currently active or not, make sure we ack any
  // pending IRQs so we don't accidentally spin out of control.
  uint8_t sts = REG_RD(&regs_->ctl_sts.b.sts);
  REG_WR(&regs_->ctl_sts.b.sts, sts);

  // Enter the lock and check to see if we should still be sending update
  // notifications.  If our channel has been nulled out, then this stream was
  // were stopped after the IRQ fired but before it was handled.  Don't send
  // any notifications in this case.
  fbl::AutoLock notif_lock(&notif_lock_);

  // TODO(johngro):  Deal with FIFO errors or descriptor errors.  There is no
  // good way to recover from such a thing.  If it happens, we need to shut
  // the stream down and send the client an error notification informing them
  // that their stream was ruined and that they need to restart it.
  if (sts & (HDA_SD_REG_STS8_FIFOE | HDA_SD_REG_STS8_DESE)) {
    REG_CLR_BITS(&regs_->ctl_sts.w, HDA_SD_REG_CTRL_RUN);
    LOG(ERROR, "Fatal stream error, shutting down DMA!  (IRQ status 0x%02x)", sts);
  }

  if (irq_channel_ == nullptr)
    return;

  if (sts & HDA_SD_REG_STS8_BCIS) {
    audio_fidl::wire::RingBufferPositionInfo position_info = {};
    position_info.position = REG_RD(&regs_->lpib);
    position_info.timestamp = zx::clock::get_monotonic().get();

    if (position_completer_) {
      position_completer_->Reply(position_info);
      position_completer_.reset();
    }
  }
}

void IntelHDAStream::DeactivateLocked() {
  // Prevent the IRQ thread from sending channel notifications by making sure
  // the irq_channel_ reference has been cleared.
  {
    fbl::AutoLock notif_lock(&notif_lock_);
    irq_channel_ = nullptr;
  }

  // If we have a connection to a client, close it.
  channel_ = nullptr;

  // Make sure that the stream has been stopped.
  EnsureStoppedLocked();

  // We are now stopped and unconfigured.
  running_ = false;
  delay_info_updated_ = false;
  fifo_depth_ = 0;
  bytes_per_frame_ = 0;

  // Release any assigned ring buffer.
  ReleaseRingBufferLocked();

  LOG(DEBUG, "Stream deactivated");
}

// Ring Buffer GetProperties.
void IntelHDAStream::GetProperties(GetPropertiesCompleter::Sync& completer) {
  fbl::AutoLock channel_lock(&channel_lock_);
  fidl::Arena allocator;
  audio_fidl::wire::RingBufferProperties properties(allocator);
  // We don't know what our FIFO depth is going to be if our format has not been set yet.
  properties.set_fifo_depth(bytes_per_frame_ ? fifo_depth_ : 0);
  // Report this properly based on the codec path delay.
  properties.set_external_delay(allocator, 0).set_needs_cache_flush_or_invalidate(true);
  completer.Reply(std::move(properties));
}

void IntelHDAStream::GetVmo(GetVmoRequestView request, GetVmoCompleter::Sync& completer) {
  zx::vmo ring_buffer_vmo;
  zx::vmo client_rb_handle;
  uint64_t tmp;
  uint32_t rb_size;

  fbl::AutoLock channel_lock(&channel_lock_);
  // We cannot change buffers while we are running, and we cannot create a
  // buffer if our format has not been set yet.
  if (running_ || (bytes_per_frame_ == 0)) {
    LOG(DEBUG, "Bad state %s%s while setting buffer.", running_ ? "(running)" : "",
        bytes_per_frame_ == 0 ? "(not configured)" : "");
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }

  // The request arguments are invalid if any of the following are true...
  //
  // 1) The user's minimum ring buffer size in frames 0.
  // 2) The user's minimum ring buffer size in bytes is too large to hold in a 32 bit integer.
  // 3) The user wants more notifications per ring than we have BDL entries.
  tmp = static_cast<uint64_t>(request->min_frames) * bytes_per_frame_;
  if ((request->min_frames == 0) || (tmp > std::numeric_limits<uint32_t>::max()) ||
      (request->clock_recovery_notifications_per_ring > MAX_BDL_LENGTH)) {
    LOG(DEBUG,
        "Invalid client args while setting buffer "
        "(min frames %u, notif/ring %u)",
        request->min_frames, request->clock_recovery_notifications_per_ring);
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInvalidArgs);
    return;
  }
  rb_size = static_cast<uint32_t>(tmp);

  // If we have an existing buffer, let go of it now.
  ReleaseRingBufferLocked();

  // Attempt to allocate a VMO for the ring buffer.
  zx_status_t status = zx::vmo::create(rb_size, 0, &ring_buffer_vmo);
  if (status != ZX_OK) {
    LOG(DEBUG, "Failed to create %u byte VMO for ring buffer (res %d)", rb_size, status);
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }

  // Commit and pin the pages for this VMO so that our HW DMA can access them.
  uint32_t hda_rights;
  hda_rights =
      (configured_type() == Type::INPUT) ? ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;

  status = pinned_ring_buffer_.Pin(ring_buffer_vmo, pci_bti_->initiator(), hda_rights);
  if (status != ZX_OK) {
    LOG(DEBUG, "Failed to commit and pin pages for %u bytes in ring buffer VMO (res %d)", rb_size,
        status);
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }

  ZX_DEBUG_ASSERT(pinned_ring_buffer_.region_count() >= 1);
  if (pinned_ring_buffer_.region_count() > MAX_BDL_LENGTH) {
    LOG(ERROR,
        "IntelHDA stream ring buffer is too fragmented (%u regions) to construct a valid BDL",
        pinned_ring_buffer_.region_count());
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }

  // Create the client's copy of this VMO with some restricted rights.
  //
  // TODO(johngro) : strip the transfer right when we move this handle.
  // Clients have no reason to be allowed to transfer the VMO to anyone else.
  //
  // TODO(johngro) : clients should not be able to change the size of the VMO,
  // but giving them the WRITE property (needed for them to be able to map the
  // VMO for write) also gives them permission to change the size of the VMO.
  status = ring_buffer_vmo.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP | ZX_RIGHT_READ |
                                         (configured_type() == Type::OUTPUT ? ZX_RIGHT_WRITE : 0),
                                     &client_rb_handle);

  if (status != ZX_OK) {
    LOG(DEBUG, "Failed duplicate ring buffer VMO handle! (res %d)", status);

    ReleaseRingBufferLocked();
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }

  // Program the buffer descriptor list.  Mark BDL entries as needed to
  // generate interrupts with the frequency requested by the user.
  uint32_t nominal_irq_spacing;
  nominal_irq_spacing = request->clock_recovery_notifications_per_ring
                            ? (rb_size + request->clock_recovery_notifications_per_ring - 1) /
                                  request->clock_recovery_notifications_per_ring
                            : 0;

  uint32_t next_irq_pos;
  uint32_t amt_done;
  uint32_t region_num, region_offset;
  uint32_t entry;
  uint32_t irqs_inserted;

  next_irq_pos = nominal_irq_spacing;
  amt_done = 0;
  region_num = 0;
  region_offset = 0;
  irqs_inserted = 0;

  for (entry = 0; (entry < MAX_BDL_LENGTH) && (amt_done < rb_size); ++entry) {
    const auto& r = pinned_ring_buffer_.region(region_num);

    if (r.size > std::numeric_limits<uint32_t>::max()) {
      LOG(DEBUG, "VMO region too large! (%" PRIu64 " bytes)", r.size);
      ReleaseRingBufferLocked();
      completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
      return;
    }

    ZX_DEBUG_ASSERT(region_offset < r.size);
    uint32_t amt_left = rb_size - amt_done;
    uint32_t region_left = static_cast<uint32_t>(r.size) - region_offset;
    uint32_t todo = std::min(amt_left, region_left);

    ZX_DEBUG_ASSERT(region_left >= DMA_ALIGN);
    bdl()[entry].flags = 0;

    if (nominal_irq_spacing) {
      uint32_t ipos = (next_irq_pos + DMA_ALIGN - 1) & ~DMA_ALIGN_MASK;

      if ((amt_done + todo) >= ipos) {
        bdl()[entry].flags = IntelHDABDLEntry::IOC_FLAG;
        next_irq_pos += nominal_irq_spacing;
        ++irqs_inserted;

        if (ipos <= amt_done) {
          todo = std::min(todo, DMA_ALIGN);
        } else {
          todo = std::min(todo, ipos - amt_done);
        }
      }
    }

    ZX_DEBUG_ASSERT(!(todo & DMA_ALIGN_MASK) || (todo == amt_left));

    bdl()[entry].address = r.phys_addr + region_offset;
    bdl()[entry].length = todo;

    ZX_DEBUG_ASSERT(!(bdl()[entry].address & DMA_ALIGN_MASK));

    amt_done += todo;
    region_offset += todo;

    if (region_offset >= r.size) {
      ZX_DEBUG_ASSERT(region_offset == r.size);
      region_offset = 0;
      region_num++;
    }
  }

  ZX_DEBUG_ASSERT(entry > 0);
  if (irqs_inserted < request->clock_recovery_notifications_per_ring) {
    bdl()[entry - 1].flags = IntelHDABDLEntry::IOC_FLAG;
  }

  if (zxlog_level_enabled(DEBUG)) {
    LOG(DEBUG, "DMA Scatter/Gather used %u entries for %u/%u bytes of ring buffer", entry, amt_done,
        rb_size);
    for (uint32_t i = 0; i < entry; ++i) {
      LOG(DEBUG, "[%2u] : %016" PRIx64 " - 0x%04x %sIRQ", i, bdl()[i].address, bdl()[i].length,
          bdl()[i].flags ? "" : "NO ");
    }
  }

  if (amt_done < rb_size) {
    ZX_DEBUG_ASSERT(entry == MAX_BDL_LENGTH);
    LOG(DEBUG, "Ran out of BDL entires after %u/%u bytes of ring buffer", amt_done, rb_size);
    ReleaseRingBufferLocked();
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }

  // TODO(johngro) : Force writeback of the cache to make sure that the BDL
  // has hit physical memory?

  // Record the cyclic buffer length and the BDL last valid index.
  ZX_DEBUG_ASSERT(entry > 0);
  cyclic_buffer_length_ = rb_size;
  bdl_last_valid_index_ = static_cast<uint16_t>(entry - 1);

  ZX_DEBUG_ASSERT((rb_size % bytes_per_frame_) == 0);
  uint32_t num_ring_buffer_frames = rb_size / bytes_per_frame_;

  // Success.  DMA is set up and ready to go.
  completer.ReplySuccess(num_ring_buffer_frames, std::move(client_rb_handle));
}

void IntelHDAStream::Start(StartCompleter::Sync& completer) {
  uint32_t ctl_val;
  const auto bdl_phys = bdl_hda_mem_.region(0).phys_addr;

  fbl::AutoLock channel_lock(&channel_lock_);
  // We cannot start unless we have configured the ring buffer and are not already started.
  bool ring_buffer_valid = pinned_ring_buffer_.region_count() >= 1;
  if (!ring_buffer_valid || running_) {
    LOG(DEBUG, "Bad state during start request %s%s.",
        !ring_buffer_valid ? "(ring buffer not configured)" : "",
        running_ ? "(already running)" : "");
    completer.Close(ZX_ERR_INTERNAL);
    return;
  }

  // Make sure that the stream DMA channel has been fully reset.
  Reset();

  // Now program all of the relevant registers before beginning operation.
  // Program the cyclic buffer length and the BDL last valid index.
  ZX_DEBUG_ASSERT((configured_type_ == Type::INPUT) || (configured_type_ == Type::OUTPUT));
  ctl_val = HDA_SD_REG_CTRL_STRM_TAG(tag_) | HDA_SD_REG_CTRL_STRIPE1 |
            (configured_type_ == Type::INPUT ? HDA_SD_REG_CTRL_DIR_IN : HDA_SD_REG_CTRL_DIR_OUT);
  REG_WR(&regs_->ctl_sts.w, ctl_val);
  REG_WR(&regs_->fmt, encoded_fmt_);
  REG_WR(&regs_->bdpl, static_cast<uint32_t>(bdl_phys & 0xFFFFFFFFu));
  REG_WR(&regs_->bdpu, static_cast<uint32_t>((bdl_phys >> 32) & 0xFFFFFFFFu));
  REG_WR(&regs_->cbl, cyclic_buffer_length_);
  REG_WR(&regs_->lvi, bdl_last_valid_index_);
  hw_wmb();

  int64_t start_time = 0;
  // Make a copy of our reference to our channel which can be used by the IRQ
  // thread to deliver notifications to the application.
  {
    fbl::AutoLock notif_lock(&notif_lock_);
    ZX_DEBUG_ASSERT(irq_channel_ == nullptr);
    irq_channel_ = channel_;

    // Set the RUN bit in our control register.  Mark the time that we did
    // so.  Do this from within the notification lock so that there is no
    // chance of us fighting with the IRQ thread over the ctl/sts register.
    // After this point in time, we may not write to the ctl/sts register
    // unless we have nerfed IRQ thread callbacks by clearing irq_channel_
    // from within the notif_lock_.
    //
    // TODO(johngro) : Do a better job of estimating when the first frame gets
    // clocked out.  For outputs, using the SSYNC register to hold off the
    // stream until the DMA has filled the FIFO could help.  There may also be a
    // way to use the WALLCLK register to determine exactly when the next HDA
    // frame will begin transmission.  Compensating for the external codec FIFO
    // delay would be a good idea as well.
    //
    // For now, we just assume that transmission starts "very soon" after we
    // whack the bit.
    constexpr uint32_t SET = HDA_SD_REG_CTRL_RUN | HDA_SD_REG_CTRL_IOCE | HDA_SD_REG_CTRL_FEIE |
                             HDA_SD_REG_CTRL_DEIE | HDA_SD_REG_STS32_ACK;
    REG_SET_BITS(&regs_->ctl_sts.w, SET);
    hw_wmb();
    start_time = zx::clock::get_monotonic().get();
  }

  // Success, we are now running.
  running_ = true;

  completer.Reply(start_time);
}

void IntelHDAStream::Stop(StopCompleter::Sync& completer) {
  fbl::AutoLock channel_lock(&channel_lock_);
  if (running_) {
    // Start by preventing the IRQ thread from processing status interrupts.
    // After we have done this, it should be safe to manipulate the ctl/sts
    // register.
    {
      fbl::AutoLock notif_lock(&notif_lock_);
      ZX_DEBUG_ASSERT(irq_channel_ != nullptr);
      irq_channel_ = nullptr;
    }

    // Make sure that we have been stopped and that all interrupts have been acked.
    EnsureStoppedLocked();
    running_ = false;
  }
  completer.Reply();
}

void IntelHDAStream::WatchClockRecoveryPositionInfo(
    WatchClockRecoveryPositionInfoCompleter::Sync& completer) {
  fbl::AutoLock lock(&notif_lock_);
  position_completer_ = completer.ToAsync();
}

void IntelHDAStream::WatchDelayInfo(WatchDelayInfoCompleter::Sync& completer) {
  if (!delay_info_updated_) {
    delay_info_updated_ = true;
    fidl::Arena allocator;
    auto delay_info = audio_fidl::wire::DelayInfo::Builder(allocator);
    // No external delay information is provided by this driver.
    delay_info.internal_delay(internal_delay_nsec_);
    completer.Reply(delay_info.Build());
  }
}

void IntelHDAStream::ReleaseRingBufferLocked() {
  pinned_ring_buffer_.Unpin();
  memset(bdl_cpu_mem_.start(), 0, bdl_cpu_mem_.size());
}

}  // namespace intel_hda
}  // namespace audio
