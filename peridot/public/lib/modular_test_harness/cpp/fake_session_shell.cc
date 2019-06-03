// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <peridot/public/lib/modular_test_harness/cpp/fake_session_shell.h>

namespace modular {
namespace testing {

void FakeSessionShell::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->svc()->Connect(session_shell_context_.NewRequest());
  session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

  component_context()->outgoing()->AddPublicService(
      session_shell_impl_.GetHandler());
}

}  // namespace testing
}  // namespace modular
