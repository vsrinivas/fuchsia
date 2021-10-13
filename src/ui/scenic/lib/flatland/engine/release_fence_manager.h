// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_RELEASE_FENCE_MANAGER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_RELEASE_FENCE_MANAGER_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/event.h>

#include <vector>

#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace flatland {

// ReleaseFenceManager is a helper which encapsulates the logic for signaling release fences, and
// for invoking frame-presented callbacks according to the contract with FrameScheduler.
//
// === Design Requirements for signaling client release fences ===
//
// Client release fences are signaled as soon as it is safe to do so without risking visual
// artifacts.  The time that it becomes safe depends on whether a frame is GPU-composited or
// direct-scanout.
//
// GPU-composition case: fences can be signaled as soon as Vulkan is finished rendering the frame.
// Direct-scanout case: client images are directly read by the display controller, and so the fences
// cannot be signaled until the *next* frame is displayed on-screen.
//
// ReleaseFenceManager handles these cases separately, in order to minimize the latency before
// clients can reuse their images.
//
// === Design Requirements for invoking FrameRenderer::FramePresentedCallback ===
//
// The contract with FrameScheduler requires that these callbacks are invoked in the order that they
// are received.  As a result, callback invocation may be delayed even though all of the information
// required by the callback is known (i.e. render-finished time and frame-presented time), when an
// earlier callback is not yet ready to invoke.
//
// For example, this can happen when a GPU-composited frame misses a vsync because rendering is
// still not finished, even though the subsequent direct-scanout frame is already on the screen.
// Ordinarily, the callback for the second frame could be invoked, but in this scenario it cannot
// because it must wait for the first callback to be invoked, which must wait until Vulkan signals
// the render-finished fence.
//
// === Usage ===
//
// ReleaseFenceManager is very simple to use.  Each frame, the caller (typically DisplayCompositor)
// calls either OnGpuCompositedFrame() or OnDirectScanoutFrame().  The caller has two other
// responsibilities:
//
// 1) for GPU-composited frames, to signal the |render_finished_event| (typically done via a Vulkan
//    semaphore).
//
// 2) to call OnVsync() when a frame is presented on the display
//
// === Thread Safety ===
//
// ReleaseFenceManager is not thread-safe; methods should only be called from the "main thread",
// i.e. the same thread as used by the |dispatcher| passed to the constructor.  Due to the use
// of this dispatcher, it is not safe to use from multiple threads even if externally
// synchronized, e.g. via a mutex.
class ReleaseFenceManager final {
 public:
  // |dispatcher| is used for waiting on the |render_finished_fence| arg to OnCpuCompositedFrame().
  explicit ReleaseFenceManager(async_dispatcher_t* dispatcher);
  ReleaseFenceManager(const ReleaseFenceManager&) = delete;
  ReleaseFenceManager(ReleaseFenceManager&&) = delete;

  // Stores a record for a new GPU-composited frame.  |frame_number| must be one larger than the
  // previous frame.  Later, when it is safe, signals |release_fences| (see class comment).
  // Invokes |frame_presented_callback| when:
  //   - |render_finished_fence| has been signaled, and:
  //   - corresponding OnVsync() has been called, and:
  //   - all previous callbacks have been invoked
  void OnGpuCompositedFrame(
      uint64_t frame_number, zx::event render_finished_fence, std::vector<zx::event> release_fences,
      scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback);

  // Stores a record for a new direct-scanout frame.  |frame_number| must be one larger than the
  // previous frame.  Later, when it is safe, signals |release_fences| (see class comment).
  // Invokes |frame_presented_callback| when:
  //   - corresponding OnVsync() has been called, and:
  //   - all previous callbacks have been invoked
  void OnDirectScanoutFrame(
      uint64_t frame_number, std::vector<zx::event> release_fences,
      scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback);

  // Called when the specified frame has appeared on screen.  |frame_number| must monotonically
  // increase with each subsequent call (repeats are OK).
  void OnVsync(uint64_t frame_number, zx::time timestamp);

  // For testing.  Return the number of frame records currently held by the manager.
  size_t frame_record_count() const { return frame_records_.size(); }

 private:
  enum class FrameType { kGpuComposition, kDirectScanout };

  struct FrameRecord {
    FrameType frame_type;

    scheduling::FrameRenderer::Timestamps timestamps;

    std::vector<zx::event> release_fences_to_signal_when_render_finished;
    std::vector<zx::event> release_fences_to_signal_when_frame_presented;

    scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback;

    // Note the relative ordering of these two fields is important because
    // during destruction we need to destruct the WaitOnce before closing
    // the handle its waiting on.
    zx::event render_finished_fence;
    std::unique_ptr<async::WaitOnce> render_finished_wait;

    // Four conditions that must be met to erase the record.  See MaybeEraseFrameRecord() comment.
    bool next_frame_started = false;
    bool frame_presented = false;
    bool render_finished = false;
    bool callback_invoked = false;
  };

  using FrameRecordMap = std::map<uint64_t, std::unique_ptr<FrameRecord>>;
  using FrameRecordIterator = FrameRecordMap::iterator;

  // Returns the record corresponding to |frame_number|, or none if no record exists.  The latter
  // condition should only be true if |frame_number| is zero.
  FrameRecordIterator FindFrameRecord(uint64_t frame_number);

  std::unique_ptr<FrameRecord> NewGpuCompositionFrameRecord(
      uint64_t frame_number, zx::event render_finished_fence,
      scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback);

  std::unique_ptr<FrameRecord> NewDirectScanoutFrameRecord(
      scheduling::FrameRenderer::FramePresentedCallback frame_presented_callback);

  void StashFrameRecord(uint64_t frame_number, std::unique_ptr<FrameRecord> record);

  // The strategy used for signaling release fences depends on whether the *previous* frame was
  // GPU-composited or direct-scanout, not the current frame. Therefore, we factor this into a
  // separate method, which is called from both OnGpuCompositedFrame() and OnDirectScanoutFrame().
  void SignalOrScheduleSignalForReleaseFences(uint64_t frame_number,
                                              FrameRecord& current_frame_record,
                                              std::vector<zx::event> release_fences);

  // In order to invoke the callback, rendering needs to be finished *and* the frame must be
  // presented, since both of these are needed to populate the timestamps in the callback arg (a
  // scheduling::FrameRenderer:FrameTimings). Although rendering is guaranteed to happen before
  // presentation, it's not guaranteed that we receive those notifications in that order.  This
  // method is a helper which allows us to invoke the callback ASAP, regardless of the order we
  // receive the notifications.
  //
  // Note: the frame-presented callback cannot be invoked unless the callbacks for all previous
  // frames have already been invoked.  This is not handled here; it is the responsibility of the
  // callers of this method.
  static bool MaybeInvokeFramePresentedCallback(FrameRecord& record);

  // If we're completely done with the frame record, erase it from the map.  There are two
  // conditions that must be met to be completely done with the frame record:
  //   1) the frame-presented callback must have been invoked
  //   2) the subsequent frame has had a chance to register any necessary fences with this frame
  //
  // Condition 2) could probably have been avoided by a different implementation.  For example, if
  // the previous frame is not present when the next frame "arrives", this could be taken as an
  // indication that the previous frame has already been renderer/presented.  But it could also
  // maybe happen because something had gone wrong.  By explicitly structuring the state machine to
  // keep the previous frame around until condition 2) has been met, it is easier to test the proper
  // functioning of this class.
  void MaybeEraseFrameRecord(FrameRecordIterator it);

  // Called from the async::WaitOnce handler on the |render_finished_fence| passed to
  // |NewGpuCompositionFrameRecord()|.
  void OnRenderFinished(uint64_t frame_number, zx::time timestamp);

  async_dispatcher_t* const dispatcher_;

  FrameRecordMap frame_records_;

  uint64_t last_frame_number_ = 0;
  uint64_t last_vsync_frame_number_ = 0;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_RELEASE_FENCE_MANAGER_H_
