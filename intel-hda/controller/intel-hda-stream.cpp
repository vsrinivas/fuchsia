// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/arch_ops.h>
#include <magenta/syscalls.h>
#include <mxtl/limits.h>
#include <string.h>

#include "drivers/audio/intel-hda/utils/utils.h"

#include "debug-logging.h"
#include "intel-hda-stream.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

constexpr size_t IntelHDAStream::MAX_BDL_LENGTH;
constexpr size_t IntelHDAStream::MAX_STREAMS_PER_CONTROLLER;

namespace {
// Note: these timeouts are arbitrary; the spec provides no guidance here.
// That said, it is hard to imagine it taking more than a single audio
// frame's worth of time, so 10mSec should be more then generous enough.
static constexpr mx_time_t IHDA_SD_MAX_RESET_TIME_NSEC  = 10000000u;  // 10mSec
static constexpr mx_time_t IHDA_SD_RESET_POLL_TIME_NSEC = 100000u;    // 100uSec
static constexpr mx_time_t IHDA_SD_STOP_HOLD_TIME_NSEC  = 100000u;
constexpr uint32_t DMA_ALIGN = 128;
constexpr uint32_t DMA_ALIGN_MASK = DMA_ALIGN - 1;
}  // namespace

void IntelHDAStream::PrintDebugPrefix() const {
    printf("[IHDA_SD #%u] ", id_);
}

IntelHDAStream::IntelHDAStream(Type                    type,
                               uint16_t                id,
                               hda_stream_desc_regs_t* regs,
                               mx_paddr_t              bdl_phys,
                               uintptr_t               bdl_virt)
    : type_(type),
      id_(id),
      regs_(regs),
      bdl_(reinterpret_cast<IntelHDABDLEntry*>(bdl_virt)),
      bdl_phys_(bdl_phys) {
    // Check the alignment restrictions
    MX_DEBUG_ASSERT(!(bdl_phys & static_cast<mx_paddr_t>(DMA_ALIGN_MASK)));
    MX_DEBUG_ASSERT(!(bdl_virt & static_cast<uintptr_t>(DMA_ALIGN_MASK)));
}

IntelHDAStream::~IntelHDAStream() {
    MX_DEBUG_ASSERT(!running_);
}

void IntelHDAStream::EnsureStopped(hda_stream_desc_regs_t* regs) {
    // Stop the stream, but do not place it into reset.  Ack any lingering IRQ
    // status bits in the process.
    REG_CLR_BITS(&regs->ctl_sts.w, HDA_SD_REG_CTRL_RUN);
    hw_wmb();
    mx_nanosleep(mx_deadline_after(IHDA_SD_STOP_HOLD_TIME_NSEC));

    constexpr uint32_t SET = HDA_SD_REG_STS32_ACK;
    constexpr uint32_t CLR = HDA_SD_REG_CTRL_IOCE |
                             HDA_SD_REG_CTRL_FEIE |
                             HDA_SD_REG_CTRL_DEIE;
    REG_MOD(&regs->ctl_sts.w, CLR, SET);
    hw_wmb();
}

void IntelHDAStream::Reset(hda_stream_desc_regs_t* regs) {
    // Enter the reset state  To do this, we...
    // 1) Clear the RUN bit if it was set.
    // 2) Set the SRST bit to 1.
    // 3) Poll until the hardware acks by setting the SRST bit to 1.
    if (REG_RD(&regs->ctl_sts.w) & HDA_SD_REG_CTRL_RUN) {
        EnsureStopped(regs);
    }

    REG_WR(&regs->ctl_sts.w, HDA_SD_REG_CTRL_SRST); // Set the reset bit.
    hw_mb();    // Make sure that all writes have gone through before we start to read.

    // Wait until the hardware acks the reset.
    mx_status_t res;
    res = WaitCondition(
            IHDA_SD_MAX_RESET_TIME_NSEC,
            IHDA_SD_RESET_POLL_TIME_NSEC,
            [](void* r) -> bool {
                auto regs = reinterpret_cast<hda_stream_desc_regs_t*>(r);
                auto val  = REG_RD(&regs->ctl_sts.w);
                return (val & HDA_SD_REG_CTRL_SRST) != 0;
            },
            regs);

    if (res != NO_ERROR) {
        GLOBAL_LOG("Failed to place stream descriptor HW into reset! (res %d)\n", res);
    }

    // Leave the reset state.  To do this, we...
    // 1) Set the SRST bit to 0.
    // 2) Poll until the hardware acks by setting the SRST bit back to 0.
    REG_WR(&regs->ctl_sts.w, 0u);
    hw_mb();    // Make sure that all writes have gone through before we start to read.

    // Wait until the hardware acks the release from reset.
    res = WaitCondition(
           IHDA_SD_MAX_RESET_TIME_NSEC,
           IHDA_SD_RESET_POLL_TIME_NSEC,
           [](void* r) -> bool {
               auto regs = reinterpret_cast<hda_stream_desc_regs_t*>(r);
               auto val  = REG_RD(&regs->ctl_sts.w);
               return (val & HDA_SD_REG_CTRL_SRST) == 0;
           },
           regs);

    if (res != NO_ERROR) {
        GLOBAL_LOG("Failed to release stream descriptor HW from reset! (res %d)\n", res);
    }
}

void IntelHDAStream::Configure(Type type, uint8_t tag) {
    if (type == Type::INVALID) {
        MX_DEBUG_ASSERT(tag == 0);
    } else {
        MX_DEBUG_ASSERT(type != Type::BIDIR);
        MX_DEBUG_ASSERT((tag != 0) && (tag < 16));
    }

    configured_type_ = type;
    tag_ = tag;
}

mx_status_t IntelHDAStream::SetStreamFormat(uint16_t encoded_fmt,
                                            const mxtl::RefPtr<DispatcherChannel>& channel) {
    if (channel == nullptr)
        return ERR_INVALID_ARGS;

    // We are being given a new format.  Reset any client connection we may have
    // and stop the hardware.
    Deactivate();

    // Record and program the stream format, then record the fifo depth we get based on this format
    // selection.
    encoded_fmt_ = encoded_fmt;
    REG_WR(&regs_->fmt, encoded_fmt_);
    hw_mb();
    fifo_depth_ = REG_RD(&regs_->fifod);

    DEBUG_LOG("Stream format set 0x%04hx; fifo is %hu bytes deep\n", encoded_fmt_, fifo_depth_);

    // Record our new client channel
    mxtl::AutoLock channel_lock(&channel_lock_);
    channel_ = channel;
    bytes_per_frame_ = StreamFormat(encoded_fmt).bytes_per_frame();

    return NO_ERROR;
}

void IntelHDAStream::Deactivate() {
    mxtl::AutoLock channel_lock(&channel_lock_);
    DeactivateLocked();
}

void IntelHDAStream::OnChannelClosed(const DispatcherChannel& channel) {
    // Is the channel being closed our currently active channel?  If so, go
    // ahead and deactivate this DMA stream.  Otherwise, just ignore this
    // request.
    mxtl::AutoLock channel_lock(&channel_lock_);

    if (&channel == channel_.get()) {
        DEBUG_LOG("Client closed channel to stream\n");
        DeactivateLocked();
    }
}

#define HANDLE_REQ(_ioctl, _payload, _handler, _allow_noack)    \
case _ioctl:                                                    \
    if (req_size != sizeof(req._payload)) {                     \
        DEBUG_LOG("Bad " #_ioctl                                \
                  " response length (%u != %zu)\n",             \
                  req_size, sizeof(req._payload));              \
        return ERR_INVALID_ARGS;                                \
    }                                                           \
    if (!_allow_noack && (req.hdr.cmd & AUDIO2_FLAG_NO_ACK)) {  \
        DEBUG_LOG("NO_ACK flag not allowed for " #_ioctl "\n"); \
        return ERR_INVALID_ARGS;                                \
    }                                                           \
    return _handler(req._payload);
mx_status_t IntelHDAStream::ProcessClientRequest(DispatcherChannel* channel,
                                                 const RequestBufferType& req,
                                                 uint32_t req_size,
                                                 mx::handle&& rxed_handle) {
    MX_DEBUG_ASSERT(channel != nullptr);

    // Is this request from our currently active channel?  If not, make sure the
    // channel has been de-activated and ignore the request.
    mxtl::AutoLock channel_lock(&channel_lock_);

    if (channel_.get() != channel) {
        channel->Deactivate(false);
        return NO_ERROR;
    }

    // Sanity check the request, then dispatch it to the appropriate handler.
    if (req_size < sizeof(req.hdr)) {
        DEBUG_LOG("Client request too small to contain header (%u < %zu)\n",
                req_size, sizeof(req.hdr));
        return ERR_INVALID_ARGS;
    }

    VERBOSE_LOG("Client Request (cmd 0x%04x tid %u) len %u\n",
                req.hdr.cmd,
                req.hdr.transaction_id,
                req_size);

    if (req.hdr.transaction_id == AUDIO2_INVALID_TRANSACTION_ID)
        return ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before deciding the dispatch target.
    auto cmd = static_cast<audio2_proto::Cmd>(req.hdr.cmd & ~AUDIO2_FLAG_NO_ACK);
    switch (cmd) {
    HANDLE_REQ(AUDIO2_RB_CMD_GET_FIFO_DEPTH, get_fifo_depth, ProcessGetFifoDepthLocked, false);
    HANDLE_REQ(AUDIO2_RB_CMD_GET_BUFFER,     get_buffer,     ProcessGetBufferLocked,    false);
    HANDLE_REQ(AUDIO2_RB_CMD_START,          start,          ProcessStartLocked,        false);
    HANDLE_REQ(AUDIO2_RB_CMD_STOP,           stop,           ProcessStopLocked,         false);
    default:
        DEBUG_LOG("Unrecognized command ID 0x%04x\n", req.hdr.cmd);
        return ERR_INVALID_ARGS;
    }
}
#undef HANDLE_REQ

void IntelHDAStream::ProcessStreamIRQ() {
    // Regarless of whether we are currently active or not, make sure we ack any
    // pending IRQs so we don't accidentally spin out of control.
    uint8_t sts = REG_RD(&regs_->ctl_sts.b.sts);
    REG_WR(&regs_->ctl_sts.b.sts, sts);

    // Enter the lock and check to see if we should still be sending update
    // notifications.  If our channel has been nulled out, then this stream was
    // were stopped after the IRQ fired but before it was handled.  Don't send
    // any notifications in this case.
    mxtl::AutoLock notif_lock(&notif_lock_);

    // TODO(johngro):  Deal with FIFO errors or descriptor errors.  There is no
    // good way to recover from such a thing.  If it happens, we need to shut
    // the stream down and send the client an error notification informing them
    // that their stream was ruined and that they need to restart it.
    if (sts & (HDA_SD_REG_STS8_FIFOE | HDA_SD_REG_STS8_DESE)) {
        REG_CLR_BITS(&regs_->ctl_sts.w, HDA_SD_REG_CTRL_RUN);
        LOG("Fatal stream error, shutting down DMA!  (IRQ status 0x%02x)\n", sts);
    }

    if (irq_channel_ == nullptr)
        return;

    if (sts & HDA_SD_REG_STS8_BCIS) {
        audio2_proto::RingBufPositionNotify msg;
        msg.hdr.cmd = AUDIO2_RB_POSITION_NOTIFY;
        msg.hdr.transaction_id = AUDIO2_INVALID_TRANSACTION_ID;
        msg.ring_buffer_pos = REG_RD(&regs_->lpib);
        irq_channel_->Write(&msg, sizeof(msg));
    }
}

void IntelHDAStream::DeactivateLocked() {
    // Prevent the IRQ thread from sending channel notifications by making sure
    // the irq_channel_ reference has been cleared.
    {
        mxtl::AutoLock notif_lock(&notif_lock_);
        irq_channel_ = nullptr;
    }

    // If we have a connection to a client, close it.
    if (channel_ != nullptr) {
        channel_->Deactivate(false);
        channel_ = nullptr;
    }

    // Make sure that the stream has been stopped.
    EnsureStoppedLocked();

    // We are now stopped and unconfigured.
    running_         = false;
    fifo_depth_      = 0;
    bytes_per_frame_ = 0;

    // Release any assigned ring buffer.
    ReleaseRingBufferLocked();

    DEBUG_LOG("Stream deactivated\n");
}

mx_status_t IntelHDAStream::ProcessGetFifoDepthLocked(
        const audio2_proto::RingBufGetFifoDepthReq& req) {
    MX_DEBUG_ASSERT(channel_ != nullptr);

    audio2_proto::RingBufGetFifoDepthResp resp;
    resp.hdr = req.hdr;

    // We don't know what our FIFO depth is going to be if our format has not
    // been set yet.
    if (bytes_per_frame_ == 0) {
        DEBUG_LOG("Bad state (not configured) while getting fifo depth.\n");
        resp.result = ERR_BAD_STATE;
        resp.fifo_depth = 0;
    } else {
        resp.result = NO_ERROR;
        resp.fifo_depth = fifo_depth_;
    }

    return channel_->Write(&resp, sizeof(resp));
}

mx_status_t IntelHDAStream::ProcessGetBufferLocked(const audio2_proto::RingBufGetBufferReq& req) {
    mx::vmo ring_buffer_vmo;
    mx::vmo client_rb_handle;
    audio2_proto::RingBufGetBufferResp resp;
    uint64_t tmp;
    uint32_t rb_size;

    MX_DEBUG_ASSERT(channel_ != nullptr);

    memset(&resp, 0, sizeof(resp));
    resp.hdr    = req.hdr;
    resp.result = ERR_INTERNAL;

    // We cannot change buffers while we are running, and we cannot create a
    // buffer if our format has not been set yet.
    if (running_ || (bytes_per_frame_ == 0)) {
        DEBUG_LOG("Bad state %s%s while setting buffer.",
                  running_ ? "(running)" : "",
                  bytes_per_frame_ == 0 ? "(not configured)" : "");
        resp.result = ERR_BAD_STATE;
        goto finished;
    }

    // The request arguments are invalid if any of the following are true...
    //
    // 1) The user's minimum ring buffer size in frames 0.
    // 2) The user's minimum ring buffer size in bytes is too large to hold in a 32 bit integer.
    // 3) The user wants more notifications per ring than we have BDL entries.
    tmp = static_cast<uint64_t>(req.min_ring_buffer_frames) * bytes_per_frame_;
    if ((req.min_ring_buffer_frames == 0) ||
        (tmp > mxtl::numeric_limits<uint32_t>::max()) ||
        (req.notifications_per_ring > MAX_BDL_LENGTH)) {
        DEBUG_LOG("Invalid client args while setting buffer "
                  "(min frames %u, notif/ring %u)\n",
                  req.min_ring_buffer_frames,
                  req.notifications_per_ring);
        resp.result = ERR_INVALID_ARGS;
        goto finished;
    }
    rb_size = static_cast<uint32_t>(tmp);

    // If we have an existing buffer, let go of it now.
    ReleaseRingBufferLocked();

    // Attempt to allocate a VMO for the ring buffer.
    resp.result = mx::vmo::create(rb_size, 0, &ring_buffer_vmo);
    if (resp.result != NO_ERROR) {
        DEBUG_LOG("Failed to create %u byte VMO for ring buffer (res %d)\n",
                rb_size, resp.result);
        goto finished;
    }

    // Create the client's copy of this VMO with some restricted rights.
    //
    // TODO(johngro) : strip the transfer right when we move this handle.
    // Clients have no reason to be allowed to transfer the VMO to anyone else.
    //
    // TODO(johngro) : clients should not be able to change the size of the VMO,
    // but giving them the WRITE property (needed for them to be able to map the
    // VMO for write) also gives them permission to change the size of the VMO.
    resp.result = ring_buffer_vmo.duplicate(
            MX_RIGHT_TRANSFER |
            MX_RIGHT_MAP |
            MX_RIGHT_READ |
            (configured_type() == Type::OUTPUT ? MX_RIGHT_WRITE : 0),
            &client_rb_handle);

    if (resp.result != NO_ERROR) {
        DEBUG_LOG("Failed duplicate ring buffer VMO handle! (res %d)\n", resp.result);
        goto finished;
    }

    // Commit the pages needed for this VMO and lock them so they cannot be
    // moved out from under the HW DMA.
    resp.result = ring_buffer_vmo.op_range(MX_VMO_OP_COMMIT, 0, rb_size, nullptr, 0);
    if (resp.result != NO_ERROR) {
        DEBUG_LOG("Failed to commit pages for %u bytes in ring buffer VMO (res %d)\n",
                rb_size, resp.result);
        goto finished;
    }

    // TODO(johngro) : Enable this when the kernel supports locking pages.
#if 0
    resp.result = ring_buffer_vmo.op_range(MX_VMO_OP_LOCK, 0, rb_size, nullptr, 0);
    if (resp.result != NO_ERROR) {
        DEBUG_LOG("Failed to lock pages for %u bytes in ring buffer VMO (res %d)\n",
                rb_size, resp.result);
        goto finished;
    }
#endif

    // Fetch the scatter-gather list for the VMO.
    VMORegion  regions[MAX_BDL_LENGTH];
    uint32_t   num_regions;

    num_regions = countof(regions);
    resp.result = GetVMORegionInfo(ring_buffer_vmo, rb_size, regions, &num_regions);
    if (resp.result != NO_ERROR) {
        DEBUG_LOG("Failed to fetch VMO scatter/gather map (res %d)\n", resp.result);
        goto finished;
    }

    // Program the buffer descriptor list.  Mark BDL entries as needed to
    // generate interrupts with the frequency requested by the user.
    uint32_t nominal_irq_spacing;
    nominal_irq_spacing = req.notifications_per_ring
                        ? (rb_size + req.notifications_per_ring - 1) /
                           req.notifications_per_ring
                        : 0;

    uint32_t next_irq_pos;
    uint32_t amt_done;
    uint32_t region_num, region_offset;
    uint32_t entry;

    next_irq_pos = nominal_irq_spacing;
    amt_done = 0;
    region_num = 0;
    region_offset = 0;

    for (entry = 0; (entry < MAX_BDL_LENGTH) && (amt_done < rb_size); ++entry) {
        MX_DEBUG_ASSERT(region_num < num_regions);
        const auto& r = regions[region_num];

        if (r.size > mxtl::numeric_limits<uint32_t>::max()) {
            DEBUG_LOG("VMO region too large! (%" PRIu64" bytes)", r.size);
            resp.result = ERR_INTERNAL;
            goto finished;
        }

        MX_DEBUG_ASSERT(region_offset < r.size);
        uint32_t amt_left    = rb_size - amt_done;
        uint32_t region_left = static_cast<uint32_t>(r.size) - region_offset;
        uint32_t todo        = mxtl::min(amt_left, region_left);

        MX_DEBUG_ASSERT(region_left >= DMA_ALIGN);
        bdl_[entry].flags = 0;

        if (nominal_irq_spacing) {
            uint32_t ipos = (next_irq_pos + DMA_ALIGN - 1) & ~DMA_ALIGN_MASK;

            if ((amt_done + todo) >= ipos) {
                bdl_[entry].flags = IntelHDABDLEntry::IOC_FLAG;
                next_irq_pos += nominal_irq_spacing;

                if (ipos <= amt_done)
                    todo = mxtl::min(todo, DMA_ALIGN);
                else
                    todo = mxtl::min(todo, ipos - amt_done);
            }
        }

        MX_DEBUG_ASSERT(!(todo & DMA_ALIGN_MASK) || (todo == amt_left));

        bdl_[entry].address = r.phys_addr + region_offset;
        bdl_[entry].length  = todo;

        MX_DEBUG_ASSERT(!(bdl_[entry].address & DMA_ALIGN_MASK));

        amt_done += todo;
        region_offset += todo;

        if (region_offset >= r.size) {
            MX_DEBUG_ASSERT(region_offset == r.size);
            region_offset = 0;
            region_num++;
        }
    }

    if (DEBUG_LOGGING) {
        DEBUG_LOG("DMA Scatter/Gather used %u entries for %u/%u bytes of ring buffer\n",
                    entry, amt_done, rb_size);
        for (uint32_t i = 0; i < entry; ++i) {
            DEBUG_LOG("[%2u] : %016" PRIx64 " - 0x%04x %sIRQ\n",
                        i,
                        bdl_[i].address,
                        bdl_[i].length,
                        bdl_[i].flags ? "" : "NO ");
        }
    }

    if (amt_done < rb_size) {
        MX_DEBUG_ASSERT(entry == MAX_BDL_LENGTH);
        DEBUG_LOG("Ran out of BDL entires after %u/%u bytes of ring buffer\n",
                  amt_done, rb_size);
        resp.result = ERR_INTERNAL;
        goto finished;
    }

    // TODO(johngro) : Force writeback of the cache to make sure that the BDL
    // has hit physical memory?

    // Record the cyclic buffer length and the BDL last valid index.
    MX_DEBUG_ASSERT(entry > 0);
    cyclic_buffer_length_ = rb_size;
    bdl_last_valid_index_ = static_cast<uint16_t>(entry - 1);

finished:
    if (resp.result == NO_ERROR) {
        // Success.  DMA is set up and ready to go.  If we manage to send the
        // client their copy of the VMO handle, keep a hold of our handle.
        // Otherwise, just let it go out of scope and be closed.
        mx_status_t res = channel_->Write(&resp, sizeof(resp), mxtl::move(client_rb_handle));
        if (res == NO_ERROR)
            ring_buffer_vmo_ = mxtl::move(ring_buffer_vmo);
        return res;
    } else {
        return channel_->Write(&resp, sizeof(resp));
    }
}

mx_status_t IntelHDAStream::ProcessStartLocked(const audio2_proto::RingBufStartReq& req) {
    audio2_proto::RingBufStartResp resp;
    uint32_t ctl_val;

    resp.hdr = req.hdr;
    resp.result = NO_ERROR;
    resp.start_ticks = 0;

    // We cannot start unless we have configured the ring buffer and are not already started.
    if (!ring_buffer_vmo_.is_valid() || running_) {
        DEBUG_LOG("Bad state during start request %s%s.\n",
                !ring_buffer_vmo_.is_valid() ? "(ring buffer not configured)" : "",
                running_ ? "(already running)" : "");
        resp.result = ERR_BAD_STATE;
        goto finished;
    }

    // Make sure that the stream DMA channel has been fully reset.
    Reset();

    // Now program all of the relevant registers before beginning operation.
    // Program the cyclic buffer length and the BDL last valid index.
    MX_DEBUG_ASSERT((configured_type_ == Type::INPUT) || (configured_type_ == Type::OUTPUT));
    ctl_val = HDA_SD_REG_CTRL_STRM_TAG(tag_)
            | HDA_SD_REG_CTRL_STRIPE1
            | (configured_type_ == Type::INPUT ? HDA_SD_REG_CTRL_DIR_IN
                                               : HDA_SD_REG_CTRL_DIR_OUT);
    REG_WR(&regs_->ctl_sts.w, ctl_val);
    REG_WR(&regs_->fmt, encoded_fmt_);
    REG_WR(&regs_->bdpl, static_cast<uint32_t>(bdl_phys_ & 0xFFFFFFFFu));
    REG_WR(&regs_->bdpu, static_cast<uint32_t>((bdl_phys_ >> 32) & 0xFFFFFFFFu));
    REG_WR(&regs_->cbl, cyclic_buffer_length_);
    REG_WR(&regs_->lvi, bdl_last_valid_index_);
    hw_wmb();

    // Make a copy of our reference to our channel which can be used by the IRQ
    // thread to deliver notifications to the application.
    {
        mxtl::AutoLock notif_lock(&notif_lock_);
        MX_DEBUG_ASSERT(irq_channel_ == nullptr);
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
        constexpr uint32_t SET = HDA_SD_REG_CTRL_RUN  |
                                 HDA_SD_REG_CTRL_IOCE |
                                 HDA_SD_REG_CTRL_FEIE |
                                 HDA_SD_REG_CTRL_DEIE |
                                 HDA_SD_REG_STS32_ACK;
        REG_SET_BITS(&regs_->ctl_sts.w, SET);
        hw_wmb();
        resp.start_ticks = mx_ticks_get();
    }

    // Success, we are now running.
    running_ = true;

finished:
    return channel_->Write(&resp, sizeof(resp));
}

mx_status_t IntelHDAStream::ProcessStopLocked(const audio2_proto::RingBufStopReq& req) {
    audio2_proto::RingBufStopResp resp;
    resp.hdr = req.hdr;

    if (running_) {
        // Start by preventing the IRQ thread from processing status interrupts.
        // After we have done this, it should be safe to manipulate the ctl/sts
        // register.
        {
            mxtl::AutoLock notif_lock(&notif_lock_);
            MX_DEBUG_ASSERT(irq_channel_ != nullptr);
            irq_channel_ = nullptr;
        }

        // Make sure that we have been stopped and that all interrupts have been acked.
        EnsureStoppedLocked();
        resp.result = NO_ERROR;
    } else {
        resp.result = ERR_BAD_STATE;
    }

    return channel_->Write(&resp, sizeof(resp));
}

void IntelHDAStream::ReleaseRingBufferLocked() {
    ring_buffer_vmo_.reset();
    MX_DEBUG_ASSERT(bdl_);
    memset(bdl_, 0, sizeof(*bdl_) * MAX_BDL_LENGTH);
}

}  // namespace intel_hda
}  // namespace audio
