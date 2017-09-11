// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/flog_viewer/binding.h"

#include "garnet/bin/flog_viewer/channel.h"
#include "lib/fxl/logging.h"

namespace flog {

Binding::Binding() {}

Binding::~Binding() {}

void Binding::SetChannel(std::shared_ptr<Channel> channel) {
  FXL_DCHECK(channel);
  FXL_DCHECK(!channel_);

  channel_ = channel;
}

ChildBinding::ChildBinding() : Binding() {}

ChildBinding::~ChildBinding() {}

void ChildBinding::SetChannel(std::shared_ptr<Channel> channel) {
  Binding::SetChannel(channel);
  channel->SetHasParent();
}

PeerBinding::PeerBinding() : Binding() {}

PeerBinding::~PeerBinding() {}

}  // namespace flog
