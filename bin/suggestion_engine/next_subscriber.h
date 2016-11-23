// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/windowed_subscriber.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"

namespace maxwell {
namespace suggestion {

// Manages a single Next suggestion subscriber.
class NextSubscriber : public BoundWindowedSubscriber<NextController> {
 public:
  NextSubscriber(
      const WindowedSubscriber::RankedSuggestions* ranked_suggestions,
      fidl::InterfaceHandle<Listener> listener)
      : BoundWindowedSubscriber(std::move(listener)) {
    SetRankedSuggestions(ranked_suggestions);
  }
};

}  // namespace suggestion
}  // namespace maxwell
