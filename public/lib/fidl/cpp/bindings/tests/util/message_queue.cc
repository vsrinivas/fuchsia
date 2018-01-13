// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/tests/util/message_queue.h"

#include <zircon/assert.h>

#include "lib/fidl/cpp/bindings/message.h"

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
  ZX_DEBUG_ASSERT(!queue_.empty());
  message->MoveFrom(queue_.front());
  Pop();
}

void MessageQueue::Pop() {
  ZX_DEBUG_ASSERT(!queue_.empty());
  delete queue_.front();
  queue_.pop();
}

}  // namespace test
}  // namespace fidl
