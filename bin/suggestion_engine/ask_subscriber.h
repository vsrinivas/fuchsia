// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/windowed_subscriber.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace suggestion {

// Manages a single Ask suggestion subscriber.
class AskSubscriber : public BoundWindowedSubscriber<AskController> {
 public:
  AskSubscriber(
      const std::vector<std::unique_ptr<RankedSuggestion>>* ranked_suggestions,
      fidl::InterfaceHandle<Listener> listener)
      : BoundWindowedSubscriber(ranked_suggestions, std::move(listener)) {}

  void SetUserInput(UserInputPtr input) override {
    // TODO(rosswang)
  }
};

}  // namespace suggestion
}  // namespace maxwell
