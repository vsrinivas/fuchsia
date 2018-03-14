// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <zircon/assert.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <string.h>

#include <intel-hda/utils/intel-hda-registers.h>

#include "debug-logging.h"
#include "intel-hda-codec.h"
#include "intel-hda-controller.h"

namespace audio {
namespace intel_hda {

void IntelHDAController::WakeupIRQThread() {
    ZX_DEBUG_ASSERT(irq_.is_valid());

    LOG(SPEW, "Waking up IRQ thread\n");
    irq_.signal(ZX_INTERRUPT_SLOT_USER, zx::time(0));
}

fbl::RefPtr<IntelHDACodec> IntelHDAController::GetCodec(uint id) {
    ZX_DEBUG_ASSERT(id < countof(codecs_));
    fbl::AutoLock codec_lock(&codec_lock_);
    return codecs_[id];
}

void IntelHDAController::WaitForIrqOrWakeup() {
    // TODO(johngro) : Fix this.  The IRQ API has changed out from under us, and
    // we cannot currently wait with a timeout.

    LOG(SPEW, "IRQ thread waiting on IRQ\n");
    uint64_t slots;
    irq_.wait(&slots);
    LOG(SPEW, "IRQ thread woke up\n");

    // Disable IRQs at the device level
    REG_WR(&regs()->intctl, 0u);
}

void IntelHDAController::SnapshotRIRB() {
    fbl::AutoLock rirb_lock(&rirb_lock_);

    ZX_DEBUG_ASSERT(rirb_ && rirb_entry_count_ && rirb_mask_);
    uint8_t rirbsts = REG_RD(&regs()->rirbsts);

    unsigned int rirb_wr_ptr = REG_RD(&regs()->rirbwp) & rirb_mask_;
    unsigned int pending     = (rirb_entry_count_ + rirb_wr_ptr - rirb_rd_ptr_) & rirb_mask_;

    // Copy the current state of the RIRB into our snapshot memory.  Note: we
    // loop at most up to 2 times in order to deal with the case where the
    // active region of the ring buffer wraps around the end.
    //
    // TODO(johngro) : Make sure to invalidate cache for the memory region
    // occupied by the RIRB before we copy into our snapshot if we are running
    // on an architecure where cache coherency is not automatically managed for
    // us via. something like snooping, or by a un-cached policy set on our
    // mapped pages in the MMU. */
    rirb_snapshot_cnt_ = 0;
    while (pending) {
         /* Intel HDA ring buffers are strange, see comments in
          * intel_hda_codec_send_cmd. */
        unsigned int tmp_rd = (rirb_rd_ptr_ + 1) & rirb_mask_;
        unsigned int todo   = fbl::min(pending, (rirb_entry_count_ - tmp_rd));

        memcpy(rirb_snapshot_ + rirb_snapshot_cnt_,
               rirb_ + tmp_rd,
               sizeof(rirb_snapshot_[0]) * todo);

        rirb_rd_ptr_ = (rirb_rd_ptr_ + todo) & rirb_mask_;
        rirb_snapshot_cnt_ += todo;
        pending -= todo;
    }

    REG_WR(&regs()->rirbsts, rirbsts);

    ZX_DEBUG_ASSERT(!pending);

    LOG(SPEW, "RIRB has %u pending responses; WP is @%u\n", rirb_snapshot_cnt_, rirb_wr_ptr);

    if (rirbsts & HDA_REG_RIRBSTS_OIS) {
        // TODO(johngro) : Implement retry behavior for codec command and
        // control.
        //
        // The OIS bit in the RIRBSTS register indicates that hardware has
        // encountered a overrun while attempting to write to the Response Input
        // Ring Buffer.  IOW - responses were received, but the controller was
        // unable to write to system memory in time, and some of the responses
        // were lost.  This should *really* never happen.  If it does, all bets
        // are pretty much off.  Every command verb sent is supposed to receive
        // a response from the codecs; if a response is dropped it can easily
        // wedge a codec's command and control state machine.
        //
        // This problem is not limited to HW being unable to write to system
        // memory in time.  There is no HW read pointer for the RIRB.  The
        // implication of this is that HW has no way to know that it has overrun
        // SW if SW is not keeping up.  If this was to happen, there would be no
        // way for the system to know, it would just look like a large number of
        // responses were lost.
        //
        // In either case, the only mitigation we could possibly implement would
        // be a reasonable retry system at the codec driver level.
        //
        // Right now, we just log the error, ack the IRQ and move on.
        LOG(ERROR, "CRITICAL ERROR: controller overrun detected while "
                   "attempting to write to response input ring buffer.\n");
    }
}

void IntelHDAController::ProcessRIRB() {
    fbl::AutoLock rirb_lock(&rirb_lock_);
    ZX_DEBUG_ASSERT(rirb_snapshot_cnt_ < HDA_RIRB_MAX_ENTRIES);
    ZX_DEBUG_ASSERT(rirb_snapshot_cnt_ < rirb_entry_count_);

    for (unsigned int i = 0; i < rirb_snapshot_cnt_; ++i) {
        auto& resp = rirb_snapshot_[i];
        resp.OnReceived();  // Fixup endianness

        /* Figure out the codec this came from */
        uint32_t caddr = resp.caddr();

        /* Sanity checks */
        if (caddr >= countof(codecs_)) {
            LOG(ERROR, "Received %ssolicited response with illegal codec address (%u) "
                       "[0x%08x, 0x%08x]\n",
                       resp.unsolicited() ? "un" : "", caddr, resp.data, resp.data_ex);
            continue;
        }

        auto codec = GetCodec(caddr);
        if (!codec) {
            LOG(ERROR, "Received %ssolicited response for non-existent codec address (%u) "
                       "[0x%08x, 0x%08x]\n",
                       resp.unsolicited() ? "un" : "", caddr, resp.data, resp.data_ex);
            continue;
        }

        LOG(TRACE, "RX[%2u]: 0x%08x%s\n",
                   caddr, resp.data, resp.unsolicited() ? " (unsolicited)" : "");

        if (!resp.unsolicited()) {
            fbl::unique_ptr<CodecCmdJob> job;

            {
                fbl::AutoLock corb_lock(&corb_lock_);

                // If this was a solicited response, there needs to be an in-flight
                // job waiting at the head of the in-flight queue which triggered
                // it.
                if (in_flight_corb_jobs_.is_empty()) {
                    LOG(ERROR,
                        "Received solicited response for codec address (%u) [0x%08x, 0x%08x] "
                        "but no in-flight job is waiting for it\n",
                        caddr, resp.data, resp.data_ex);
                    continue;
                }

                // Grab the front of the in-flight queue.
                job = in_flight_corb_jobs_.pop_front();
            }

            // Sanity checks complete.  Pass the response and the job which
            // triggered it on to the codec.
            codec->ProcessSolicitedResponse(resp, fbl::move(job));
        } else {
            auto codec = GetCodec(caddr);
            if (!codec) {
                LOG(ERROR,
                    "Received unsolicited response for non-existent codec address (%u) "
                    "[0x%08x, 0x%08x]\n", caddr, resp.data, resp.data_ex);
                continue;
            }

            codec->ProcessUnsolicitedResponse(resp);
        }
    }

    rirb_snapshot_cnt_ = 0;
}

void IntelHDAController::SendCodecCmdLocked(CodecCommand cmd) {
    ZX_DEBUG_ASSERT(corb_space_ > 0);

    // Write the command into the ring buffer and update the SW shadow of the
    // write pointer.  We will update the HW write pointer later on when we
    // commit the new CORB commands.
    //
    // Note: Intel's ring buffers are a bit wonky.  See Section 4.4.1.4, but the
    // general idea is that to send a command, you do *not* write the command at
    // WP and then bump the WP.  Instead you write the command to (WP + 1) %
    // RING_SIZE, then update WP to be (WP + 1) % RING_SIZE.  IOW - The write
    // pointer always points to the last command written, not the place where
    // the next command will go.  This behavior holds in the RIRB direction as
    // well.
    corb_wr_ptr_ = (corb_wr_ptr_ + 1) & corb_mask_;
    corb_[corb_wr_ptr_].data = htole32(cmd.data);
    corb_space_--;
}

zx_status_t IntelHDAController::QueueCodecCmd(fbl::unique_ptr<CodecCmdJob>&& job) {
    ZX_DEBUG_ASSERT(job != nullptr);
    LOG(TRACE, "TX: Codec ID %u Node ID %hu Verb 0x%05x\n",
               job->codec_id(), job->nid(), job->verb().val);

    // Enter the lock, then check out the state of the ring buffer.  If the
    // buffer is full, or if there are already commands backed up into the
    // pending queue, just add the job to the end of the pending queue.
    // Otherwise, actually write the command into the CORB, add the job to the
    // end of the in-flight queue, and wakeup the IRQ thread.
    //
    fbl::AutoLock corb_lock(&corb_lock_);
    ZX_DEBUG_ASSERT(corb_wr_ptr_ < corb_entry_count_);
    ZX_DEBUG_ASSERT(corb_);

    if (!corb_space_) {
        // If we have no space in the CORB, there must be some jobs which are
        // currently in-flight.
        ZX_DEBUG_ASSERT(!in_flight_corb_jobs_.is_empty());
        pending_corb_jobs_.push_back(fbl::move(job));
    } else {
        // Alternatively, if there is space in the CORB, the pending job queue
        // had better be empty.
        ZX_DEBUG_ASSERT(pending_corb_jobs_.is_empty());
        SendCodecCmdLocked(job->command());
        in_flight_corb_jobs_.push_back(fbl::move(job));
    }

    CommitCORBLocked();

    return ZX_OK;
}

void IntelHDAController::ProcessCORB() {
    fbl::AutoLock corb_lock(&corb_lock_);

    // Check IRQ status for the CORB
    uint8_t corbsts = REG_RD(&regs()->corbsts);
    REG_WR(&regs()->corbsts, corbsts);

    if (corbsts & HDA_REG_CORBSTS_MEI) {
        // TODO(johngro) : Implement proper controller reset behavior.
        //
        // The MEI bit in CORBSTS indicates some form memory error detected by
        // the controller while attempting to read from system memory.  This is
        // Extremely Bad and should never happen.  If it does, the TRM suggests
        // that all bets are off, and the only reasonable action is to
        // completely shutdown and reset the controller.
        //
        // Right now, we do not implement this behavior.  Instead we log, then
        // assert in debug builds.  In release builds, we simply ack the
        // interrupt and move on.
        //
        LOG(ERROR, "CRITICAL ERROR: controller encountered an unrecoverable "
                   "error attempting to read from system memory!\n");
        ZX_DEBUG_ASSERT(false);
    }

    // Figure out how much space we have in the CORB
    ComputeCORBSpaceLocked();

    // While we have room in the CORB, and still have commands which are waiting
    // to be sent out, move commands from the pending queue into the in-flight
    // queue.
    LOG(SPEW, "CORB has space for %u commands; WP is @%u\n", corb_space_, corb_wr_ptr_);
    while (corb_space_ && !pending_corb_jobs_.is_empty()) {
        auto job = pending_corb_jobs_.pop_front();

        SendCodecCmdLocked(job->command());

        in_flight_corb_jobs_.push_back(fbl::move(job));
    }
    LOG(SPEW, "Update CORB WP; WP is @%u\n", corb_wr_ptr_);

    // Update the CORB write pointer.
    CommitCORBLocked();
}

void IntelHDAController::ComputeCORBSpaceLocked() {
    ZX_DEBUG_ASSERT(corb_entry_count_ && corb_mask_);
    ZX_DEBUG_ASSERT(corb_wr_ptr_ == REG_RD(&regs()->corbwp));

    unsigned int corb_rd_ptr = REG_RD(&regs()->corbrp) & corb_mask_;
    unsigned int corb_used   = (corb_entry_count_ + corb_wr_ptr_ - corb_rd_ptr) & corb_mask_;

    /* The way the Intel HDA command ring buffers work, it is impossible to ever
     * be using more than N - 1 of the ring buffer entries.  Our available
     * space should be the ring buffer size, minus the amt currently used, minus 1 */
    ZX_DEBUG_ASSERT(corb_entry_count_   >  corb_used);
    ZX_DEBUG_ASSERT(corb_max_in_flight_ >= corb_used);
    corb_space_ = corb_max_in_flight_ - corb_used;
}

void IntelHDAController::CommitCORBLocked() {
    // TODO(johngro) : Make sure to force a write back of the cache for the
    // dirty portions of the CORB before we update the write pointer if we are
    // running on an architecure where cache coherency is not automatically
    // managed for us via. snooping or by an explicit uncached or write-thru
    // policy set on our mapped pages in the MMU.
    ZX_DEBUG_ASSERT(regs());
    ZX_DEBUG_ASSERT(corb_entry_count_ && corb_mask_);
    ZX_DEBUG_ASSERT(corb_wr_ptr_ < corb_entry_count_);
    REG_WR(&regs()->corbwp, corb_wr_ptr_);
}

void IntelHDAController::ProcessStreamIRQ(uint32_t intsts) {
    for (uint32_t i = 0; intsts; i++, intsts >>= 1) {
        if (intsts & 0x1) {
            ZX_DEBUG_ASSERT(i < countof(all_streams_));
            ZX_DEBUG_ASSERT(all_streams_[i] != nullptr);
            all_streams_[i]->ProcessStreamIRQ();
        }
    }
}

void IntelHDAController::ProcessControllerIRQ() {
    // Start by checking for codec wake events.
    uint16_t statests = REG_RD(&regs()->statests) & HDA_REG_STATESTS_MASK;
    if (statests) {
        REG_WR(&regs()->statests, statests);
        uint32_t tmp = statests;
        for (uint8_t i = 0u; statests && (i < countof(codecs_)); ++i, tmp >>= 1) {
            if (!(tmp & 1u))
                continue;

            // TODO(johngro) : How is a codec supposed to signal a hot unplug
            // event?  Docs clearly indicate that they can be hot plugged, and
            // that you detect hot plug events by enabling wake events and
            // checking the STATESTS register when you receive one, but they
            // don't seem to give any indication of how to detect that a codec
            // has been unplugged.
            if (codecs_[i] == nullptr) {
                codecs_[i] = IntelHDACodec::Create(*this, i);

                // If we successfully created our codec, attempt to start it up.
                // If it fails to start, release our reference to the codec.
                if ((codecs_[i] != nullptr) && (codecs_[i]->Startup() != ZX_OK)) {
                    codecs_[i] = nullptr;
                }
            } else {
                codecs_[i]->ProcessWakeupEvt();
            }
        }
    }
}

int IntelHDAController::IRQThread() {
    // TODO(johngro) : Raise our thread priority here.

    // Compute the set of interrupts we may be interested in during operation.
    uint32_t interesting_irqs = HDA_REG_INTCTL_GIE | HDA_REG_INTCTL_CIE;
    for (uint32_t i = 0; i < countof(all_streams_); ++i) {
        if (all_streams_[i] != nullptr)
            interesting_irqs |= HDA_REG_INTCTL_SIE(i);
    }

    // Wait until we have been published and given the go-ahead to operate
    while (GetState() == State::STARTING)
        WaitForIrqOrWakeup();

    // Clear our STATESTS shadow, setup the WAKEEN register to wake us
    // up if there is any change to the codec enumeration status.
    REG_SET_BITS(&regs()->wakeen, HDA_REG_STATESTS_MASK);

    // Allow unsolicited codec responses
    REG_SET_BITS(&regs()->gctl, HDA_REG_GCTL_UNSOL);

    while (GetState() != State::SHUTTING_DOWN) {
        // Enable interrupts at the top level and wait for there to be Great
        // Things to do.
        REG_WR(&regs()->intctl, interesting_irqs);
        WaitForIrqOrWakeup();
        if (GetState() == State::SHUTTING_DOWN)
            break;

        // Take a snapshot of any pending responses ASAP in order to minimize
        // the chance of an RIRB overflow.  We will process the responses which
        // we snapshot-ed in a short while after we are done handling other
        // important IRQ tasks.
        SnapshotRIRB();

        uint32_t intsts = REG_RD(&regs()->intsts);

        if (intsts & HDA_REG_INTCTL_SIE_MASK)
            ProcessStreamIRQ(intsts & HDA_REG_INTCTL_SIE_MASK);

        if (intsts & HDA_REG_INTCTL_CIE)
            ProcessControllerIRQ();

        ProcessRIRB();
        ProcessCORB();
    }

    LOG(TRACE, "IRQ thread exiting!\n");

    // Disable all interrupts and place the device into reset on our way out.
    REG_WR(&regs()->intctl, 0u);
    REG_CLR_BITS(&regs()->gctl, HDA_REG_GCTL_HWINIT);

    // Tell all the codecs to begin the process of shutting down.  Then wait for
    // them to finish.
    for (auto& codec_ptr : codecs_)
        codec_ptr->BeginShutdown();

    for (auto& codec_ptr : codecs_) {
        codec_ptr->FinishShutdown();
        codec_ptr.reset();
    }

    // Any CORB jobs we may have had in progress may be discarded.
    {
        fbl::AutoLock corb_lock(&corb_lock_);
        in_flight_corb_jobs_.clear();
        pending_corb_jobs_.clear();
    }

    // Done.  Clearly mark that we are now shut down.
    SetState(State::SHUT_DOWN);
    return 0;
}

}  // namespace intel_hda
}  // namespace audio
