// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/ask_channel.h"
#include "apps/maxwell/src/suggestion_engine/windowed_subscriber.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"

namespace maxwell {
namespace suggestion {

// Manages a single Ask suggestion subscriber.
class AskSubscriber : public BoundWindowedSubscriber<AskController> {
 public:
  AskSubscriber(fidl::InterfaceHandle<Listener> listener)
      : BoundWindowedSubscriber(std::move(listener)), channel_(this) {
    SetRankedSuggestions(channel_.ranked_suggestions());
  }

  void SetUserInput(UserInputPtr input) override {
    // TODO(rosswang)
  }

 private:
  AskChannel channel_;
};

}  // namespace suggestion
}  // namespace maxwell
