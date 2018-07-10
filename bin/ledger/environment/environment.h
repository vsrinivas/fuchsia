// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_
#define PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_

#include <lib/async/dispatcher.h>
#include <lib/backoff/backoff.h>
#include <lib/fit/function.h>

#include "peridot/bin/ledger/coroutine/coroutine.h"

namespace ledger {

// Environment for the ledger application. |io_async| is optional, but if
// provided in the constructor, |async| must outlive |io_async|.
class Environment {
 public:
  using BackoffFactory = fit::function<std::unique_ptr<backoff::Backoff>()>;
  Environment(async_t* async, async_t* io_async,
              std::unique_ptr<coroutine::CoroutineService> coroutine_service,
              BackoffFactory backoff_factory);
  Environment(Environment&& other) noexcept;
  ~Environment();

  Environment& operator=(Environment&& other) noexcept;

  async_t* async() { return async_; }

  // Returns the async_t to be used for I/O operations.
  async_t* io_async() { return io_async_; }

  coroutine::CoroutineService* coroutine_service() {
    return coroutine_service_.get();
  }

  std::unique_ptr<backoff::Backoff> MakeBackoff();

 private:
  async_t* async_ = nullptr;

  // The async_t to be used for I/O operations.
  async_t* io_async_ = nullptr;

  std::unique_ptr<coroutine::CoroutineService> coroutine_service_;
  BackoffFactory backoff_factory_;
};

// Builder for the environment.
//
// The |SetAsync| method must be called before the environment can be build.
class EnvironmentBuilder {
 public:
  EnvironmentBuilder();
  ~EnvironmentBuilder();

  EnvironmentBuilder(const EnvironmentBuilder& other) = delete;
  EnvironmentBuilder(EnvironmentBuilder&& other) = delete;
  EnvironmentBuilder& operator=(const EnvironmentBuilder& other) = delete;
  EnvironmentBuilder& operator=(EnvironmentBuilder&& other) = delete;

  EnvironmentBuilder& SetAsync(async_t* async);
  EnvironmentBuilder& SetIOAsync(async_t* io_async);
  EnvironmentBuilder& SetCoroutineService(
      std::unique_ptr<coroutine::CoroutineService> coroutine_service);
  EnvironmentBuilder& SetBackoffFactory(
      Environment::BackoffFactory backoff_factory);

  Environment Build();

 private:
  async_t* async_ = nullptr;
  async_t* io_async_ = nullptr;
  std::unique_ptr<coroutine::CoroutineService> coroutine_service_;
  Environment::BackoffFactory backoff_factory_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_
