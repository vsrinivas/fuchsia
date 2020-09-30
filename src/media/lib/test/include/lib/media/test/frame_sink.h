// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_FRAME_SINK_H_
#define SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_FRAME_SINK_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/vmo.h>
#include <stdint.h>

#include <memory>
#include <set>

#include "src/lib/fxl/macros.h"
#include "src/lib/ui/base_view/view_provider_component.h"

class FrameSinkView;

// FrameSink will deliver frames to Scenic via ImagePipe, with a presentation
// timestamp set to when PutFrame() was called. This should visually show decode
// throughput, for now.  A .h264 file doesn't have real timestamps, but we could
// potentially add a flag to clock the frames out at a particular frame rate.
//
// This class is designed such that frames, views, and stream signal methods
// should be called on the main_loop_. WaitForView must be called on a separate
// thread while main_loop_ runs to allow Scenic time to connect before we send
// frames.
class FrameSink {
 public:
  // Only designed to be managed via unique_ptr<>, so this static factory method
  // is how we create these.
  //
  // |startup_context| must out-last the FrameSink
  // |main_loop| must out-last the FrameSink
  // |frames_per_second| if not exactly 0.0, the frames per second, else use the
  //     built-in default frames per second.
  static std::unique_ptr<FrameSink> Create(sys::ComponentContext* component_context,
                                           async::Loop* main_loop, double frames_per_second,
                                           fit::function<void(FrameSink*)> view_connected_callback);

  ~FrameSink();

  // The on_done will get called on main_loop_'s thread.  If the callee
  // wants/needs to, the callee can post to a different thread.
  void PutFrame(uint32_t image_id, zx::vmo vmo, uint64_t vmo_offset,
                std::shared_ptr<const fuchsia::media::StreamOutputFormat> output_format,
                fit::closure on_done);

  // (Quickly) cause all frames to eventually be returned (assuming the rest of
  // the system is cooperating), without forcing discard of previously queued
  // frames, and asynchronously call on_frames_returned when all frames are done
  // returning.
  //
  // The on_frames_returned gets called on main_loop_'s thread.  If the callee
  // wants/needs to, the callee can post to a different thread.
  void PutEndOfStreamThenWaitForFramesReturnedAsync(fit::closure on_frames_returned);

  // Can be called on any thread.
  uint32_t GetPendingCount() const;

  void AddFrameSinkView(FrameSinkView* view);
  void RemoveFrameSinkView(FrameSinkView* view);

  // This is not used for any calls to PutFrame(), rather only internally
  // within calls to PutEndOfStreamThenWaitForFramesReturnedAsync().  This is
  // public only so that FrameSinkView can see it.
  constexpr static uint32_t kBlankFrameImageId = std::numeric_limits<uint32_t>::max();

 private:
  FrameSink(sys::ComponentContext* component_context, async::Loop* main_loop,
            double frames_per_second, fit::function<void(FrameSink*)> view_connected_callback);

  void CheckIfAllFramesReturned();

  sys::ComponentContext* component_context_ = nullptr;
  async::Loop* main_loop_ = nullptr;
  const double frames_per_second_ = 0.0;

  // The actual views are owned by the view_provider_app_ with no
  // super-straightforward way for PutFrame() to find them, so we instead have
  // our views register themselves with the FrameSink as they're
  // created/destroyed, and we make sure to only interact with them on main_loop
  // so we don't need a lock for the set of views. These pointers are not owning
  // pointers.  Any given pointer in here is removed from the set in the view's
  // destructor, so that avoids this set pointing to a view that doesn't exist.
  // Typically there will be only one view in steady-state in typical usage of
  // FrameSink, but this should still allow for more than one.
  std::set<FrameSinkView*> views_;

  // During destruction of view_provider_app_, some views_.erase() happens.  For
  // this reason, and because we want to be able to assert in ~FrameSink that
  // there are zero views, we use a unique_ptr<> here so we can delete
  // view_provider_app_ early during ~FrameSink.
  std::unique_ptr<scenic::ViewProviderComponent> view_provider_component_;

  std::atomic<uint32_t> frames_outstanding_ = 0;

  fit::closure on_frames_returned_;

  fit::function<void(FrameSink*)> view_connected_callback_;

  zx_time_t last_requested_present_time_ = ZX_TIME_INFINITE_PAST;

  FXL_DISALLOW_COPY_AND_ASSIGN(FrameSink);
};

#endif  // SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_FRAME_SINK_H_
