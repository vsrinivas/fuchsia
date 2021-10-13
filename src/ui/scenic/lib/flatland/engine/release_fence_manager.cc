// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/release_fence_manager.h"

#include <lib/async/cpp/time.h>
#include <lib/syslog/cpp/macros.h>

namespace {

void SignalAll(const std::vector<zx::event>& events) {
  for (auto& e : events) {
    e.signal(0u, ZX_EVENT_SIGNALED);
  }
}

}  // namespace

namespace flatland {

ReleaseFenceManager::ReleaseFenceManager(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {
  FX_DCHECK(dispatcher_);
}

void ReleaseFenceManager::OnGpuCompositedFrame(
    uint64_t frame_number, zx::event render_finished_fence, std::vector<zx::event> release_fences,
    scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback) {
  auto record = NewGpuCompositionFrameRecord(frame_number, std::move(render_finished_fence),
                                             std::move(frame_presented_callback));
  SignalOrScheduleSignalForReleaseFences(frame_number, *record, std::move(release_fences));
  StashFrameRecord(frame_number, std::move(record));
}

void ReleaseFenceManager::OnDirectScanoutFrame(
    uint64_t frame_number, std::vector<zx::event> release_fences,
    scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback) {
  auto record = NewDirectScanoutFrameRecord(std::move(frame_presented_callback));
  SignalOrScheduleSignalForReleaseFences(frame_number, *record, std::move(release_fences));
  StashFrameRecord(frame_number, std::move(record));
}

void ReleaseFenceManager::OnVsync(uint64_t frame_number, zx::time timestamp) {
  FX_DCHECK(frame_number >= last_vsync_frame_number_);
  last_vsync_frame_number_ = frame_number;

  // Any previous frames which haven't already been presented have been skipped; they will never
  // show up on-screen.  Any release fences associated with them should be signaled at this time.
  // Additionally, it *may* be possible to invoke the frame-presented callback for some or all of
  // these frames... but only if all previous callbacks have been invoked.  This is due to the
  // contract with FrameScheduler, which dictates that callbacks must be invoked in order.
  const auto begin_it = frame_records_.lower_bound(0);
  const auto end_it = frame_records_.upper_bound(frame_number);
  bool all_earlier_callbacks_were_invoked = true;

  for (auto it = begin_it; it != end_it;) {
    auto& record = *it->second;
    if (!record.frame_presented) {
      record.frame_presented = true;
      record.timestamps.actual_presentation_time = timestamp;

      SignalAll(record.release_fences_to_signal_when_frame_presented);
      record.release_fences_to_signal_when_frame_presented.clear();

      // The contract with the FrameScheduler dictates that callbacks must be invoked in order.
      // Therefore, if we reach a record whose callback cannot be invoked (e.g. because that frame
      // is GPU-composited and hasn't finished rendering), then no subsequent callback can be
      // invoked, even if all other conditions are met.
      if (all_earlier_callbacks_were_invoked) {
        if (!MaybeInvokeFramePresentedCallback(record)) {
          all_earlier_callbacks_were_invoked = false;
        }
      }
    }

    // If we're completely finished with this frame record then erase it, otherwise keep it around.
    // Either way, keep iterating: we still need to mark other frames as presented, and set their
    // presentation time.
    //
    // Note: the iterator is incremented at the call-site (i.e. here) before the unincremented value
    // is potentially erased by MaybeEraseFrameRecord().  This is why we don't put "++it" into the
    // enclosing for-statement: if the previous entry is erased, then we would increment an invalid
    // iterator.
    MaybeEraseFrameRecord(it++);
  }
}

bool ReleaseFenceManager::MaybeInvokeFramePresentedCallback(FrameRecord& record) {
  FX_DCHECK(!record.callback_invoked) << "callback already invoked.";

  // Both conditions must be true to invoke the callback.
  if (record.render_finished && record.frame_presented) {
    // It would be nice to DCHECK(record.render_done_time <= record.actual_presentation_time),
    // however this is not possible. In the case of a dropped GPU-composited frame, it is possible
    // for a subsequent direct-scanout frame to be presented on-screen while the dropped frame is
    // still being rendered.  Since the first/dropped frame gets the same |actual_presentation_time|
    // as the next frame, this would be earlier than the |render_done_time|.
    record.frame_presented_callback(record.timestamps);
    record.frame_presented_callback = nullptr;
    record.callback_invoked = true;
    return true;
  }
  return false;
}

void ReleaseFenceManager::MaybeEraseFrameRecord(FrameRecordIterator it) {
  FrameRecord& r = *it->second;
  if (r.callback_invoked && r.next_frame_started) {
    FX_DCHECK(r.frame_presented) << "callback shouldn't have been invoked: frame not presented.";
    FX_DCHECK(r.render_finished) << "callback shouldn't have been invoked: render not finished.";
    frame_records_.erase(it);
  }
}

void ReleaseFenceManager::SignalOrScheduleSignalForReleaseFences(
    uint64_t frame_number, FrameRecord& current_frame, std::vector<zx::event> release_fences) {
  auto previous_frame_it = FindFrameRecord(frame_number - 1);
  if (previous_frame_it == frame_records_.end()) {
    // Signal the fences immediately, since there is no previous frame whose content corresponds to
    // these fences.
    SignalAll(release_fences);
    return;
  }
  FrameRecord& previous_frame = *previous_frame_it->second;

  FX_DCHECK(!previous_frame.next_frame_started);
  previous_frame.next_frame_started = true;

  switch (previous_frame.frame_type) {
    case FrameType::kGpuComposition: {
      // Signal the fences as soon as the previous frame has finished rendering.  This may have
      // already occurred; if so, signal the fences immediately.  Otherwise, stash the fences to be
      // signaled later, when rendering is finished.  This is preferable to to setting up an
      // async::Wait() here, because we already had to set one up when we received the previous
      // frame, so we might as well piggy-back on that.
      if (previous_frame.render_finished) {
        SignalAll(release_fences);
      } else {
        FX_DCHECK(previous_frame.release_fences_to_signal_when_render_finished.empty());
        std::move(std::begin(release_fences), std::end(release_fences),
                  std::back_inserter(previous_frame.release_fences_to_signal_when_render_finished));
      }
    } break;
    case FrameType::kDirectScanout: {
      // Stash these fences to be signaled later, when the frame is presented (this will become
      // known when the manager is notified of a vsync event).
      FX_DCHECK(!current_frame.frame_presented);
      FX_DCHECK(current_frame.release_fences_to_signal_when_frame_presented.empty());
      std::move(std::begin(release_fences), std::end(release_fences),
                std::back_inserter(current_frame.release_fences_to_signal_when_frame_presented));
    } break;
  }

  // It's possible that the previous frame was already finished (i.e. callback was already invoked),
  // and it was just waiting around so that this frame could figure out what to do.
  MaybeEraseFrameRecord(previous_frame_it);
}

std::unique_ptr<ReleaseFenceManager::FrameRecord> ReleaseFenceManager::NewGpuCompositionFrameRecord(
    uint64_t frame_number, zx::event render_finished_fence,
    scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback) {
  FX_DCHECK(render_finished_fence);
  auto record = std::make_unique<ReleaseFenceManager::FrameRecord>();
  record->frame_type = FrameType::kGpuComposition;
  record->frame_presented_callback = std::move(frame_presented_callback);

  // Set up a waiter on the |render_finished_fence|.
  record->render_finished_wait = std::make_unique<async::WaitOnce>(
      render_finished_fence.get(), ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_TIMESTAMP);
  // Keep the fence alive as long as the WaitOnce.
  record->render_finished_fence = std::move(render_finished_fence);

  zx_status_t wait_status = record->render_finished_wait->Begin(
      dispatcher_, [this, frame_number](async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                                        const zx_packet_signal_t* signal_info) {
        FX_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED) << "unexpected status: " << status;
        if (status == ZX_ERR_CANCELED) {
          // Must return immediately if canceled.  In particular, we cannot rely on the validity of
          // the |this| pointer, because we may have been canceled due to the destruction of the
          // manager (because this would the destruction of all frame-records, and hence also this
          // wait).
          return;
        }

        // The real work is done here.
        OnRenderFinished(frame_number, zx::time(signal_info->timestamp));
      });
  FX_DCHECK(wait_status == ZX_OK);

  return record;
}

std::unique_ptr<ReleaseFenceManager::FrameRecord> ReleaseFenceManager::NewDirectScanoutFrameRecord(
    scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback) {
  auto record = std::make_unique<ReleaseFenceManager::FrameRecord>();
  record->frame_type = FrameType::kDirectScanout;
  record->frame_presented_callback = std::move(frame_presented_callback);

  // TODO(fxbug.dev/74455): might want to add an offset to the time, so we don't screw up the
  // FrameScheduler. Another idea would be to use zero, and have the FrameScheduler ignore such
  // values.
  record->render_finished = true;
  record->timestamps.render_done_time = async::Now(dispatcher_);

  return record;
}

void ReleaseFenceManager::StashFrameRecord(uint64_t frame_number,
                                           std::unique_ptr<FrameRecord> record) {
  FX_DCHECK(frame_number == last_frame_number_ + 1);
  last_frame_number_ = frame_number;
  frame_records_[frame_number] = std::move(record);
}

ReleaseFenceManager::FrameRecordIterator ReleaseFenceManager::FindFrameRecord(
    uint64_t frame_number) {
  auto it = frame_records_.find(frame_number);

  // This is an invariant that should be maintained by the rest of ReleaseFenceManager.  However,
  // if it is violated, this method is nevertheless careful not to return a pointer to bogus memory.
  FX_DCHECK((frame_number == 0) == (it == frame_records_.end()))
      << "Should find a record for any frame #, except frame 0.  Requested frame #: "
      << frame_number;

  return it;
}

void ReleaseFenceManager::OnRenderFinished(uint64_t frame_number, zx::time timestamp) {
  auto it = FindFrameRecord(frame_number);

  // Signal fences and do bookkeeping/cleanup associated with render-finished.
  {
    auto& record = *it->second;
    record.render_finished = true;
    record.timestamps.render_done_time = timestamp;
    SignalAll(record.release_fences_to_signal_when_render_finished);
    record.release_fences_to_signal_when_render_finished.clear();
    record.render_finished_wait.reset();  // safe from within WaitOnce() closure.
  }

  // If there are previous frames whose callback hasn't been invoked, we cannot invoke the
  // callback for this frame either, due to the contract with FrameScheduler that callbacks
  // must be invoked in the order received.
  {
    auto begin_it = frame_records_.begin();
    if (it != begin_it) {
      // Records with invoked callbacks are always erased immediately, unless they are the
      // last frame (i.e. no subsequent frame), in which case they are kept around until the
      // next frame (and then are erased immediately).
      FX_DCHECK(begin_it->second->callback_invoked == false)
          << "If callback was invoked, the record should have been erased.";

      // If the current frame isn't the first frame, and the first frame's callback hasn't
      // been invoked, then we can't invoke the callback for the current frame.
      return;
    }
  }

  // Reaching this point, we know that all previous frame-presented callbacks have been
  // invoked.  Now, signal as many frame-presented callbacks as we can, starting with the
  // current frame record, iterating forward until a frame is reached whose callback cannot be
  // invoked, or there are no more frames.
  while (it != frame_records_.end()) {
    auto current_it = it++;
    bool invoked_callback = MaybeInvokeFramePresentedCallback(*current_it->second);
    MaybeEraseFrameRecord(current_it);
    if (!invoked_callback)
      return;
  }
}

}  // namespace flatland
