// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/sequence_checker.h>
#include <lib/async/sequence_id.h>
#include <zircon/assert.h>

namespace async {

namespace {

constexpr const char kSequenceNotSupported[] = "The async_dispatcher_t need to support sequences.";

constexpr const char kWrongThread[] =
    "Current thread is not executing a task managed by the dispatcher.";

constexpr const char kNoSequence[] =
    "Current thread is executing a task managed by the dispatcher, but the task is not associated "
    "with a sequence.";

async_sequence_id_t ensure_valid_sequence_id(async_dispatcher_t* dispatcher) {
  async_sequence_id_t current;
  zx_status_t status = async_get_sequence_id(dispatcher, &current);
  ZX_ASSERT_MSG(status != ZX_ERR_NOT_SUPPORTED, "%s", kSequenceNotSupported);
  ZX_ASSERT_MSG(status != ZX_ERR_INVALID_ARGS, "%s", kWrongThread);
  ZX_ASSERT_MSG(status != ZX_ERR_WRONG_TYPE, "%s", kNoSequence);
  ZX_ASSERT(status == ZX_OK);
  return current;
}

}  // namespace

sequence_checker::sequence_checker(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {
  self_ = ensure_valid_sequence_id(dispatcher);
}

bool sequence_checker::is_sequence_valid() const {
  async_sequence_id_t current = ensure_valid_sequence_id(dispatcher_);
  return self_.value == current.value;
}

synchronization_checker::synchronization_checker(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {
  async_sequence_id_t current;
  zx_status_t status = async_get_sequence_id(dispatcher, &current);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    self_ = std::this_thread::get_id();
  } else {
    // If the async runtime supports sequences, the current thread must be
    // running on one.
    ZX_ASSERT_MSG(status != ZX_ERR_INVALID_ARGS, "%s", kWrongThread);
    ZX_ASSERT_MSG(status != ZX_ERR_WRONG_TYPE, "%s", kNoSequence);
    ZX_ASSERT(status == ZX_OK);
    self_ = current;
  }
}

bool synchronization_checker::is_synchronized() const {
  if (cpp17::holds_alternative<async_sequence_id_t>(self_)) {
    const async_sequence_id_t& initial = cpp17::get<async_sequence_id_t>(self_);
    async_sequence_id_t current = ensure_valid_sequence_id(dispatcher_);
    return current.value == initial.value;
  }

  const std::thread::id& initial = cpp17::get<std::thread::id>(self_);
  return std::this_thread::get_id() == initial;
}

}  // namespace async
