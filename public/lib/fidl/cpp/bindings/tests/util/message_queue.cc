// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/tests/util/message_queue.h"

#include "lib/fidl/cpp/bindings/message.h"
#include "lib/fxl/logging.h"

namespace fidl {
namespace test {

MessageQueue::MessageQueue() {}

MessageQueue::~MessageQueue() {
  while (!queue_.empty())
    Pop();
}

bool MessageQueue::IsEmpty() const {
  return queue_.empty();
}

void MessageQueue::Push(Message* message) {
  auto* new_message = new AllocMessage();
  new_message->MoveHandlesFrom(message);
  new_message->CopyDataFrom(message);
  queue_.push(new_message);
}

void MessageQueue::Pop(AllocMessage* message) {
  FXL_DCHECK(!queue_.empty());
  queue_.front()->MoveTo(message);
  Pop();
}

void MessageQueue::Pop() {
  FXL_DCHECK(!queue_.empty());
  delete queue_.front();
  queue_.pop();
}

}  // namespace test
}  // namespace fidl
