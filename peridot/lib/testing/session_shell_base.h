// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_SESSION_SHELL_BASE_H_
#define PERIDOT_LIB_TESTING_SESSION_SHELL_BASE_H_

#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/session_shell_impl.h"

namespace modular {
namespace testing {

class SessionShellBase : public ComponentBase<void> {
 public:
  SessionShellBase(sys::ComponentContext* const component_context)
      : ComponentBase(component_context) {
    component_context->svc()->Connect(session_shell_context_.NewRequest());

    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    component_context->outgoing()->AddPublicService(session_shell_impl_.GetHandler());
  }

 protected:
  modular::testing::SessionShellImpl* session_shell_impl() { return &session_shell_impl_; }

  fuchsia::modular::SessionShellContext* session_shell_context() {
    return session_shell_context_.get();
  }

  fuchsia::modular::StoryProvider* story_provider() { return story_provider_.get(); }

 private:
  modular::testing::SessionShellImpl session_shell_impl_;
  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionShellBase);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_SESSION_SHELL_BASE_H_
