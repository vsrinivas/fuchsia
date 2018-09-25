// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/environment/environment.h"

#include <lib/backoff/exponential_backoff.h>
#include <lib/fxl/macros.h>
#include <lib/timekeeper/system_clock.h>

#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/lib/ledger_client/constants.h"

namespace ledger {

Environment::Environment(
    async_dispatcher_t* dispatcher, async_dispatcher_t* io_dispatcher,
    std::string firebase_api_key,
    std::unique_ptr<coroutine::CoroutineService> coroutine_service,
    BackoffFactory backoff_factory, std::unique_ptr<timekeeper::Clock> clock)
    : dispatcher_(dispatcher),
      io_dispatcher_(io_dispatcher),
      firebase_api_key_(std::move(firebase_api_key)),
      coroutine_service_(std::move(coroutine_service)),
      backoff_factory_(std::move(backoff_factory)),
      clock_(std::move(clock)) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(coroutine_service_);
  FXL_DCHECK(backoff_factory_);
  FXL_DCHECK(clock_);
}

Environment::Environment(Environment&& other) noexcept {
  *this = std::move(other);
}

Environment& Environment::operator=(Environment&& other) noexcept {
  dispatcher_ = other.dispatcher_;
  io_dispatcher_ = other.io_dispatcher_;
  firebase_api_key_ = std::move(other.firebase_api_key_);
  coroutine_service_ = std::move(other.coroutine_service_);
  backoff_factory_ = std::move(other.backoff_factory_);
  clock_ = std::move(other.clock_);
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(coroutine_service_);
  FXL_DCHECK(backoff_factory_);
  FXL_DCHECK(clock_);
  return *this;
}

Environment::~Environment() {}

std::unique_ptr<backoff::Backoff> Environment::MakeBackoff() {
  return backoff_factory_();
}

EnvironmentBuilder::EnvironmentBuilder()
    : firebase_api_key_(modular::kFirebaseApiKey) {}

EnvironmentBuilder::~EnvironmentBuilder() {}

EnvironmentBuilder& EnvironmentBuilder::SetAsync(
    async_dispatcher_t* dispatcher) {
  dispatcher_ = dispatcher;
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetIOAsync(
    async_dispatcher_t* io_dispatcher) {
  io_dispatcher_ = io_dispatcher;
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetFirebaseApiKey(
    std::string firebase_api_key) {
  firebase_api_key_ = std::move(firebase_api_key);
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetCoroutineService(
    std::unique_ptr<coroutine::CoroutineService> coroutine_service) {
  coroutine_service_ = std::move(coroutine_service);
  return *this;
}

// public
EnvironmentBuilder& EnvironmentBuilder::SetBackoffFactory(
    Environment::BackoffFactory backoff_factory) {
  backoff_factory_ = std::move(backoff_factory);
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetClock(
    std::unique_ptr<timekeeper::Clock> clock) {
  clock_ = std::move(clock);
  return *this;
}

Environment EnvironmentBuilder::Build() {
  if (!coroutine_service_) {
    coroutine_service_ = std::make_unique<coroutine::CoroutineServiceImpl>();
  }
  if (!backoff_factory_) {
    backoff_factory_ = [] {
      return std::make_unique<backoff::ExponentialBackoff>();
    };
  }
  if (!clock_) {
    clock_ = std::make_unique<timekeeper::SystemClock>();
  }
  return Environment(dispatcher_, io_dispatcher_, std::move(firebase_api_key_),
                     std::move(coroutine_service_), std::move(backoff_factory_),
                     std::move(clock_));
}

}  // namespace ledger
