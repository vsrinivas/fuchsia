// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/tests/fakes/fake_session.h"

#include <lib/syslog/cpp/macros.h>

namespace root_presenter {
namespace testing {

FakeSession::FakeSession() : binding_(this) {}

FakeSession::~FakeSession() {}

void FakeSession::Bind(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> request,
                       fuchsia::ui::scenic::SessionListenerPtr listener) {
  binding_.Bind(std::move(request));
  listener_ = std::move(listener);
}

void FakeSession::Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) {
  for (auto it = cmds.begin(); it != cmds.end(); ++it) {
    last_cmds_.push_back(std::move(*it));
  }
}

void FakeSession::Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
                          std::vector<zx::event> release_fences, PresentCallback callback) {
  presents_called_++;
}

void FakeSession::Present(uint64_t presentation_time, PresentCallback callback) {
  presents_called_++;
}

void FakeSession::RequestPresentationTimes(zx_duration_t request_prediction_span,
                                           RequestPresentationTimesCallback callback) {
  callback({.remaining_presents_in_flight_allowed = 1});
}

void FakeSession::Present2(fuchsia::ui::scenic::Present2Args args, Present2Callback callback) {
  presents_called_++;

  fuchsia::scenic::scheduling::FramePresentedInfo info = {};
  info.num_presents_allowed = 1;
  info.presentation_infos.push_back({});
  binding_.events().OnFramePresented(std::move(info));
}
}  // namespace testing
}  // namespace root_presenter
