// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/windowed_subscriber.h"

namespace maxwell {

class AskChannel;

// Manages a single Ask suggestion subscriber.
class AskSubscriber : public BoundWindowedSubscriber<AskController> {
 public:
  AskSubscriber(AskChannel* channel,
                fidl::InterfaceHandle<SuggestionListener> listener,
                fidl::InterfaceRequest<AskController> controller);

  void SetUserInput(UserInputPtr input) override;

 private:
  AskChannel* channel_;
};

}  // namespace maxwell
