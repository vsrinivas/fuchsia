// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_CONTEXT_HANDLER_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_CONTEXT_HANDLER_H_

#include <string>
#include <vector>

#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/services/user/intelligence_services.fidl.h"
#include "apps/modular/lib/fidl/context.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

// Keeps track of the current Context (in the maxwell sense of the word) for
// user runner and story runner, specifically for the purpose of computing story
// importance. Therefore, it watches only selected topics. This will be revised
// later. The dimensions of context and their current values are available form
// values().
class ContextHandler : maxwell::ContextListener {
 public:
  explicit ContextHandler(maxwell::IntelligenceServices* intelligence_services);
  ~ContextHandler() override;

  // Selects topics to watch for and subscribes to the context engine for updates.
  //
  // To be notified of updates, register a listener by calling Watch() before
  // calling SelectTopics().
  void SelectTopics(const std::vector<fidl::String>& topics);

  void Watch(const std::function<void()>& watcher);

  const ContextState& values() const { return state_; }

 private:
  // |ContextListener|
  void OnContextUpdate(maxwell::ContextUpdatePtr update) override;

  fidl::InterfacePtr<maxwell::ContextReader> context_reader_;

  // Current value of the context.
  ContextState state_;

  fidl::Binding<maxwell::ContextListener> binding_;
  std::vector<std::function<void()>> watchers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextHandler);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_CONTEXT_HANDLER_H_
