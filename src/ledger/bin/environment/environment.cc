// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/environment/environment.h"

#include <lib/backoff/exponential_backoff.h>
#include <lib/timekeeper/system_clock.h>
#include <src/lib/fxl/macros.h>

#include "peridot/lib/ledger_client/constants.h"
#include "peridot/lib/rng/system_random.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"

namespace ledger {

Environment::Environment(
    bool disable_statistics, async_dispatcher_t* dispatcher,
    async_dispatcher_t* io_dispatcher, std::string firebase_api_key,
    sys::ComponentContext* component_context,
    std::unique_ptr<coroutine::CoroutineService> coroutine_service,
    BackoffFactory backoff_factory, std::unique_ptr<timekeeper::Clock> clock,
    std::unique_ptr<rng::Random> random)
    : disable_statistics_(disable_statistics),
      dispatcher_(dispatcher),
      io_dispatcher_(io_dispatcher),
      firebase_api_key_(std::move(firebase_api_key)),
      component_context_(component_context),
      coroutine_service_(std::move(coroutine_service)),
      backoff_factory_(std::move(backoff_factory)),
      clock_(std::move(clock)),
      random_(std::move(random)) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(io_dispatcher_);
  FXL_DCHECK(component_context_);
  FXL_DCHECK(coroutine_service_);
  FXL_DCHECK(backoff_factory_);
  FXL_DCHECK(clock_);
  FXL_DCHECK(random_);
}

Environment::Environment(Environment&& other) noexcept {
  *this = std::move(other);
}

Environment& Environment::operator=(Environment&& other) noexcept {
  disable_statistics_ = other.disable_statistics_;
  dispatcher_ = other.dispatcher_;
  io_dispatcher_ = other.io_dispatcher_;
  firebase_api_key_ = std::move(other.firebase_api_key_);
  component_context_ = other.component_context_;
  coroutine_service_ = std::move(other.coroutine_service_);
  backoff_factory_ = std::move(other.backoff_factory_);
  clock_ = std::move(other.clock_);
  random_ = std::move(other.random_);
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(io_dispatcher_);
  FXL_DCHECK(component_context_);
  FXL_DCHECK(coroutine_service_);
  FXL_DCHECK(backoff_factory_);
  FXL_DCHECK(clock_);
  FXL_DCHECK(random_);
  return *this;
}

Environment::~Environment() {}

std::unique_ptr<backoff::Backoff> Environment::MakeBackoff() {
  return backoff_factory_();
}

EnvironmentBuilder::EnvironmentBuilder()
    : firebase_api_key_(modular::kFirebaseApiKey) {}

EnvironmentBuilder::~EnvironmentBuilder() {}

EnvironmentBuilder& EnvironmentBuilder::SetDisableStatistics(
    bool disable_statistics) {
  disable_statistics_ = disable_statistics;
  return *this;
}

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

EnvironmentBuilder& EnvironmentBuilder::SetStartupContext(
    sys::ComponentContext* component_context) {
  component_context_ = component_context;
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

EnvironmentBuilder& EnvironmentBuilder::SetRandom(
    std::unique_ptr<rng::Random> random) {
  random_ = std::move(random);
  return *this;
}

Environment EnvironmentBuilder::Build() {
  if (!coroutine_service_) {
    coroutine_service_ = std::make_unique<coroutine::CoroutineServiceImpl>();
  }
  if (!clock_) {
    clock_ = std::make_unique<timekeeper::SystemClock>();
  }
  if (!random_) {
    random_ = std::make_unique<rng::SystemRandom>();
  }
  if (!backoff_factory_) {
    backoff_factory_ = [random = random_.get()] {
      return std::make_unique<backoff::ExponentialBackoff>(
          random->NewBitGenerator<uint64_t>());
    };
  }
  return Environment(disable_statistics_, dispatcher_, io_dispatcher_,
                     std::move(firebase_api_key_), component_context_,
                     std::move(coroutine_service_), std::move(backoff_factory_),
                     std::move(clock_), std::move(random_));
}

}  // namespace ledger
