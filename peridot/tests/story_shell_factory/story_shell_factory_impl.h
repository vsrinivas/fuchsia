// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_STORY_SHELL_FACTORY_STORY_SHELL_FACTORY_IMPL_H_
#define PERIDOT_TESTS_STORY_SHELL_FACTORY_STORY_SHELL_FACTORY_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/time/time_delta.h>

#include <string>

namespace modular {
namespace testing {

// An implementation of the fuchsia.modular.StoryShellFactory FIDL service, to
// be used in session shell components in integration tests.
class StoryShellFactoryImpl : fuchsia::modular::StoryShellFactory {
 public:
  using StoryShellRequest =
      fidl::InterfaceRequest<fuchsia::modular::StoryShell>;

  StoryShellFactoryImpl();
  virtual ~StoryShellFactoryImpl() override;

  // Produces a handler function that can be used in the outgoing service
  // provider.
  fidl::InterfaceRequestHandler<fuchsia::modular::StoryShellFactory>
  GetHandler();

  // Whenever StoryShellFactory.AttachStory() is called, the supplied callback
  // is invoked with the story ID and StoryShell request.
  void set_on_attach_story(
      fit::function<void(std::string story_id, StoryShellRequest request)>
          callback) {
    on_attach_story_ = std::move(callback);
  }

  // Whenever StoryShellFactory.DetachStory() is called, the supplied callback
  // is invoked. The return callback of DetachStory() is invoked asynchronously
  // after a delay that can be configured by the client with set_detach_delay().
  void set_on_detach_story(fit::function<void()> callback) {
    on_detach_story_ = std::move(callback);
  }

  // Configures the delay after which the return callback of DetachStory() is
  // invoked. Used to test the timeout behavior of sessionmgr.
  void set_detach_delay(zx::duration detach_delay) {
    detach_delay_ = detach_delay;
  }

 private:
  // |StoryShellFactory|
  void AttachStory(std::string story_id,
                   StoryShellRequest story_shell) override;

  // |StoryShellFactory|
  void DetachStory(std::string story_id, fit::function<void()> done) override;

  fidl::BindingSet<fuchsia::modular::StoryShellFactory> bindings_;
  fit::function<void(std::string story_id, StoryShellRequest request)>
      on_attach_story_{[](std::string, StoryShellRequest) {}};
  fit::function<void()> on_detach_story_{[]() {}};
  zx::duration detach_delay_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryShellFactoryImpl);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_TESTS_STORY_SHELL_FACTORY_STORY_SHELL_FACTORY_IMPL_H_
