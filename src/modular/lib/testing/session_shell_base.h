// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_SESSION_SHELL_BASE_H_
#define SRC_MODULAR_LIB_TESTING_SESSION_SHELL_BASE_H_

#include "src/modular/lib/testing/component_base.h"
#include "src/modular/lib/testing/session_shell_impl.h"

namespace modular_testing {

class SessionShellBase : public ComponentBase<void> {
 public:
  SessionShellBase(sys::ComponentContext* const component_context)
      : ComponentBase(component_context) {
    component_context->svc()->Connect(session_shell_context_.NewRequest());

    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    component_context->outgoing()->AddPublicService(session_shell_impl_.GetHandler());
  }

 protected:
  modular_testing::SessionShellImpl* session_shell_impl() { return &session_shell_impl_; }

  fuchsia::modular::SessionShellContext* session_shell_context() {
    return session_shell_context_.get();
  }

  fuchsia::modular::StoryProvider* story_provider() { return story_provider_.get(); }

 private:
  modular_testing::SessionShellImpl session_shell_impl_;
  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionShellBase);
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_TESTING_SESSION_SHELL_BASE_H_
