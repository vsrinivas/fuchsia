// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_STORY_RUNNER_CONTEXT_HANDLER_H_
#define PERIDOT_BIN_STORY_RUNNER_CONTEXT_HANDLER_H_

#include <string>
#include <vector>

#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/user_intelligence/fidl/intelligence_services.fidl.h"
#include "peridot/lib/fidl/context.h"

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

  // Selects topics to watch for and subscribes to the context engine for
  // updates.
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

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextHandler);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_CONTEXT_HANDLER_H_
