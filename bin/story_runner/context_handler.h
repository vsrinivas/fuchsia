// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_CONTEXT_HANDLER_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_CONTEXT_HANDLER_H_

#include <string>

#include "apps/maxwell/services/context/context_provider.fidl.h"
#include "apps/maxwell/services/user/intelligence_services.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

// Keeps track of the current Context (in the maxwell sense of the word) for
// user runner and story runner. The dimensions of context and their current
// values are available form values().
class ContextHandler : maxwell::ContextListener {
 public:
  ContextHandler(maxwell::IntelligenceServices* intelligence_services);

  ~ContextHandler() override;

  const fidl::Map<fidl::String, fidl::String>& values() const {
    return value_->values;
  }

 private:
  // |ContextListener|
  void OnUpdate(maxwell::ContextUpdatePtr value) override;

  fidl::InterfacePtr<maxwell::ContextProvider> context_provider_;

  // Current value of the context.
  maxwell::ContextUpdatePtr value_;

  fidl::Binding<maxwell::ContextListener> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextHandler);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_CONTEXT_HANDLER_H_
