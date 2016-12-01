// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/ask_subscriber.h"

#include "apps/maxwell/src/suggestion_engine/ask_channel.h"

namespace maxwell {

AskSubscriber::AskSubscriber(AskChannel* channel,
                             fidl::InterfaceHandle<Listener> listener,
                             fidl::InterfaceRequest<AskController> controller)
    : BoundWindowedSubscriber(channel->ranked_suggestions(),
                              std::move(listener),
                              std::move(controller)),
      channel_(channel) {}

void AskSubscriber::SetUserInput(UserInputPtr input) {
  channel_->SetQuery(input->get_text());
}

}  // namespace maxwell
