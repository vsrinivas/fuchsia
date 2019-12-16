// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/session_test.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/scheduling/constant_frame_predictor.h"
#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void SessionTest::SetUp() {
  ErrorReportingTest::SetUp();

  frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
      std::make_shared<scheduling::VsyncTiming>(),
      std::make_unique<scheduling::ConstantFramePredictor>(/* static_vsync_offset */ zx::msec(5)));

  session_context_ = CreateSessionContext();
  session_ = CreateSession();
}

void SessionTest::TearDown() {
  session_.reset();
  frame_scheduler_.reset();

  ErrorReportingTest::TearDown();
}

SessionContext SessionTest::CreateSessionContext() {
  FXL_DCHECK(frame_scheduler_);

  SessionContext session_context{.vk_device = vk::Device(),
                                 .escher = nullptr,
                                 .escher_resource_recycler = nullptr,
                                 .escher_image_factory = nullptr,
                                 .escher_rounded_rect_factory = nullptr,
                                 .release_fence_signaller = nullptr,
                                 .frame_scheduler = frame_scheduler_,
                                 .scene_graph = nullptr,
                                 .view_linker = nullptr};
  return session_context;
}

CommandContext SessionTest::CreateCommandContext() {
  // By default, return the empty command context.
  return CommandContext();
}

std::unique_ptr<Session> SessionTest::CreateSession() {
  static uint32_t next_id = 1;
  return std::make_unique<Session>(next_id++, session_context_, shared_event_reporter(),
                                   shared_error_reporter());
}

bool SessionTest::Apply(::fuchsia::ui::gfx::Command command) {
  auto command_context = CreateCommandContext();
  auto retval = session_->ApplyCommand(&command_context, std::move(command));
  command_context.Flush();
  return retval;
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
