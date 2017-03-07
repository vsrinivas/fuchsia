// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/resources/resource.h"

#include "escher/impl/command_buffer.h"

namespace escher {

Resource2::Resource2(std::unique_ptr<ResourceCore> core)
    : core_(std::move(core)) {}

Resource2::~Resource2() {
  core_->manager_->ReceiveResourceCore(std::move(core_));
}

void Resource2::KeepAlive(impl::CommandBuffer* command_buffer) {
  auto sequence_number = command_buffer->sequence_number();
  if (sequence_number != core_->sequence_number()) {
    core_->set_sequence_number(sequence_number);
    KeepDependenciesAlive(command_buffer);
  }
}

}  // namespace escher
