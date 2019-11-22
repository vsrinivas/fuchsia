// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/tests/fakes/fake_session.h"

#include "src/lib/fxl/logging.h"

namespace root_presenter {
namespace testing {

FakeSession::FakeSession() : binding_(this) {}

FakeSession::~FakeSession() {}

void FakeSession::Bind(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> request,
                       fuchsia::ui::scenic::SessionListenerPtr listener) {
  binding_.Bind(std::move(request));
  listener_ = std::move(listener);
}

void FakeSession::Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) {}

void FakeSession::Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
                          std::vector<zx::event> release_fences, PresentCallback callback) {}

void FakeSession::Present(uint64_t presentation_time, PresentCallback callback) {}

void FakeSession::RequestPresentationTimes(zx_duration_t request_prediction_span,
                                           RequestPresentationTimesCallback callback) {}

void FakeSession::Present2(fuchsia::ui::scenic::Present2Args args, Present2Callback callback) {}
}  // namespace testing
}  // namespace root_presenter
