// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/navigation_processor.h"

namespace modular {

NavigationProcessor::NavigationProcessor() = default;
NavigationProcessor::~NavigationProcessor() = default;

void NavigationProcessor::RegisterListener(
    fidl::InterfaceHandle<fuchsia::modular::NavigationListener> listener) {
  listeners_.AddInterfacePtr(listener.Bind());
}

void NavigationProcessor::Navigate(
    fuchsia::modular::NavigationAction navigation) {
  for (const auto& listener : listeners_.ptrs()) {
    (*listener)->OnNavigation(navigation);
  }
}

}  // namespace modular
