// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/environment/environment.h"

#include <lib/backoff/exponential_backoff.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/coroutine/coroutine_impl.h"

namespace ledger {

Environment::Environment(
    async_dispatcher_t* dispatcher, async_dispatcher_t* io_dispatcher,
    std::unique_ptr<coroutine::CoroutineService> coroutine_service,
    BackoffFactory backoff_factory)
    : dispatcher_(dispatcher),
      io_dispatcher_(io_dispatcher),
      coroutine_service_(std::move(coroutine_service)),
      backoff_factory_(std::move(backoff_factory)) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(coroutine_service_);
  FXL_DCHECK(backoff_factory_);
}

Environment::Environment(Environment&& other)
    : Environment(other.dispatcher_, other.io_dispatcher_,
                  std::move(other.coroutine_service_),
                  std::move(other.backoff_factory_)) {}

Environment& Environment::operator=(Environment&& other) {
  dispatcher_ = other.dispatcher_;
  io_dispatcher_ = other.io_dispatcher_;
  coroutine_service_ = std::move(other.coroutine_service_);
  backoff_factory_ = std::move(other.backoff_factory_);
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(coroutine_service_);
  FXL_DCHECK(backoff_factory_);
  return *this;
}

Environment::~Environment() {}

std::unique_ptr<backoff::Backoff> Environment::MakeBackoff() {
  return backoff_factory_();
}

EnvironmentBuilder::EnvironmentBuilder() {}

EnvironmentBuilder::~EnvironmentBuilder() {}

EnvironmentBuilder& EnvironmentBuilder::SetAsync(async_dispatcher_t* dispatcher) {
  dispatcher_ = dispatcher;
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetIOAsync(async_dispatcher_t* io_dispatcher) {
  io_dispatcher_ = io_dispatcher;
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

Environment EnvironmentBuilder::Build() {
  if (!coroutine_service_) {
    coroutine_service_ = std::make_unique<coroutine::CoroutineServiceImpl>();
  }
  if (!backoff_factory_) {
    backoff_factory_ = [] {
      return std::make_unique<backoff::ExponentialBackoff>();
    };
  }
  return Environment(dispatcher_, io_dispatcher_, std::move(coroutine_service_),
                     std::move(backoff_factory_));
}

}  // namespace ledger
