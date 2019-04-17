// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/fakes/fake_scenic.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/lib/fxl/logging.h"

namespace media_player {
namespace test {

FakeScenic::FakeScenic() : dispatcher_(async_get_default_dispatcher()) {}

FakeScenic::~FakeScenic() {}

void FakeScenic::CreateSession(
    ::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
    ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
  fake_session_.Bind(std::move(session), listener.Bind());
}

void FakeScenic::GetDisplayInfo(GetDisplayInfoCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void FakeScenic::GetDisplayOwnershipEvent(
    GetDisplayOwnershipEventCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void FakeScenic::TakeScreenshot(TakeScreenshotCallback callback) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace test
}  // namespace media_player
