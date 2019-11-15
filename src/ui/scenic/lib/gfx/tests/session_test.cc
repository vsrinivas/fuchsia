// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/session_test.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/engine/constant_frame_predictor.h"
#include "src/ui/scenic/lib/gfx/engine/default_frame_scheduler.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void SessionTest::SetUp() {
  ErrorReportingTest::SetUp();

  display_ = std::make_shared<Display>(
      /*id*/ 0, /* width */ 0, /* height */ 0);
  frame_scheduler_ = std::make_shared<DefaultFrameScheduler>(
      display_, std::make_unique<ConstantFramePredictor>(/* static_vsync_offset */ zx::msec(5)));

  session_context_ = CreateSessionContext();
  session_ = CreateSession();
}

void SessionTest::TearDown() {
  session_.reset();
  frame_scheduler_.reset();
  display_.reset();

  ErrorReportingTest::TearDown();
}

SessionContext SessionTest::CreateSessionContext() {
  FXL_DCHECK(frame_scheduler_);

  SessionContext session_context{
      vk::Device(),
      nullptr,           // escher::Escher*
      nullptr,           // escher::ResourceRecycler
      nullptr,           // escher::ImageFactory*
      nullptr,           // escher::RoundedRectFactory*
      nullptr,           // escher::ReleaseFenceSignaller*
      frame_scheduler_,  // shared_ptr<FrameScheduler>
      nullptr,           // SceneGraphWeakPtr
      nullptr,           // ResourceLinker*
      nullptr            // ViewLinker*
  };
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
