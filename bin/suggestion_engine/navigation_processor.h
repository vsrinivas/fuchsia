// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_NAVIGATION_PROCESSOR_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_NAVIGATION_PROCESSOR_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/suggestion_engine/decision_policies/decision_policy.h"

namespace modular {

// The NavigationProcessor dispatches received navigation events.
class NavigationProcessor {
 public:
  NavigationProcessor();
  ~NavigationProcessor();

  // Add listener that will be notified when a navigation event comes.
  void RegisterListener(
      fidl::InterfaceHandle<fuchsia::modular::NavigationListener> listener);

  // Immediately send the navigation event to listeners.
  void Navigate(fuchsia::modular::NavigationAction navigation);

 private:
  fidl::InterfacePtrSet<fuchsia::modular::NavigationListener> listeners_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_NAVIGATION_PROCESSOR_H_
