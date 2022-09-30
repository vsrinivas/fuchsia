// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/sequence_checker.h>
#include <lib/async/sequence_id.h>
#include <zircon/assert.h>

namespace async {

namespace {

constexpr const char kWrongThread[] =
    "Access from multiple threads detected. "
    "This is not allowed. Ensure the object is used from the same thread.";

}  // namespace

sequence_checker::sequence_checker(async_dispatcher_t* dispatcher,
                                   const char* application_description)
    : dispatcher_(dispatcher),
      application_description_(application_description != nullptr ? application_description : "") {
  async_sequence_id_t current;
  const char* error;
  zx_status_t status = async_get_sequence_id(dispatcher, &current, &error);
  if (status != ZX_OK) {
    ZX_PANIC("%s %s", application_description_, error);
  }
  self_ = current;
}

cpp17::variant<cpp17::monostate, std::string> sequence_checker::is_sequence_valid() const {
  const char* error;
  zx_status_t status = async_check_sequence_id(dispatcher_, self_, &error);
  if (status != ZX_OK) {
    return std::string(application_description_) + " " + error;
  }
  return cpp17::monostate{};
}

void sequence_checker::lock() const __TA_ACQUIRE() {
  cpp17::variant<cpp17::monostate, std::string> result = is_sequence_valid();
  if (cpp17::holds_alternative<std::string>(result)) {
    ZX_PANIC("%s", cpp17::get<std::string>(result).c_str());
  }
}

synchronization_checker::synchronization_checker(async_dispatcher_t* dispatcher,
                                                 const char* application_description)
    : dispatcher_(dispatcher),
      application_description_(application_description != nullptr ? application_description : "") {
  async_sequence_id_t current;
  const char* error;
  zx_status_t status = async_get_sequence_id(dispatcher, &current, &error);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    self_ = std::this_thread::get_id();
  } else {
    // If the async runtime supports sequences, the current thread must be
    // running on one.
    if (status != ZX_OK) {
      ZX_PANIC("%s %s", application_description, error);
    }
    self_ = current;
  }
}

cpp17::variant<cpp17::monostate, std::string> synchronization_checker::is_synchronized() const {
  if (cpp17::holds_alternative<async_sequence_id_t>(self_)) {
    const char* error;
    zx_status_t status =
        async_check_sequence_id(dispatcher_, cpp17::get<async_sequence_id_t>(self_), &error);
    if (status != ZX_OK) {
      return std::string(application_description_) + " " + error;
    }
    return cpp17::monostate{};
  }

  const std::thread::id& initial = cpp17::get<std::thread::id>(self_);
  if (std::this_thread::get_id() != initial) {
    return std::string(application_description_) + " " + kWrongThread;
  }
  return cpp17::monostate{};
}

void synchronization_checker::lock() const __TA_ACQUIRE() {
  cpp17::variant<cpp17::monostate, std::string> result = is_synchronized();
  if (cpp17::holds_alternative<std::string>(result)) {
    ZX_PANIC("%s", cpp17::get<std::string>(result).c_str());
  }
}

}  // namespace async
