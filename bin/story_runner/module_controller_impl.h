// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_MODULE_CONTROLLER_IMPL_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_MODULE_CONTROLLER_IMPL_H_

#include <vector>

#include "apps/modular/services/story/module.fidl.h"
#include "apps/modular/services/story/module_controller.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

class StoryImpl;

// Implements the ModuleController interface, which is given to the
// client that called Story.StartModule(). At most one
// ModuleControllerImpl instance is associated with each
// StoryConnection instance.
class ModuleControllerImpl : public ModuleController {
 public:
  ModuleControllerImpl(
      StoryImpl* const story_impl,
      const fidl::String& url,
      fidl::InterfacePtr<Module> module,
      fidl::InterfaceRequest<ModuleController> module_controller);

  ~ModuleControllerImpl() override = default;

  // Notifies all watchers of Done.
  void Done();

  // Notifies all watchers of error, and also remembers the error
  // state in the error_ flag. A newly added Watcher is notified of an
  // existing error state.
  void Error();

  // Calls Stop() on the module, closes the module handle, notifies
  // watchers, then DisposeModule()s the connection and finally calls
  // done(). Thus, done must not reference anything in
  // ModuleController or the related StoryConnection.
  void TearDown(std::function<void()> done);

 private:
  // |ModuleController|
  void Watch(fidl::InterfaceHandle<ModuleWatcher> watcher) override;
  void Stop(const StopCallback& done) override;

  StoryImpl* const story_impl_;
  const fidl::String url_;
  fidl::InterfacePtr<Module> module_;
  fidl::Binding<ModuleController> binding_;
  std::vector<fidl::InterfacePtr<ModuleWatcher>> watchers_;
  bool error_{false};

  // Callbacks of TearDown() invocations. If there is one Stop()
  // request pending, a second one is only queued, no second call to
  // Stop() is made.
  std::vector<std::function<void()>> teardown_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleControllerImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_MODULE_CONTROLLER_IMPL_H_
