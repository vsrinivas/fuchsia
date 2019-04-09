// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENVIRONMENT_ENVIRONMENT_H_
#define SRC_LEDGER_BIN_ENVIRONMENT_ENVIRONMENT_H_

#include <lib/async/dispatcher.h>
#include <lib/backoff/backoff.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/timekeeper/clock.h>

#include <memory>

#include "peridot/lib/rng/random.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace ledger {

// Environment for the ledger application. |dispatcher| must outlive
// |io_dispatcher|.
class Environment {
 public:
  using BackoffFactory = fit::function<std::unique_ptr<backoff::Backoff>()>;
  Environment(bool disable_statistics, async_dispatcher_t* dispatcher,
              async_dispatcher_t* io_dispatcher, std::string firebase_api_key,
              sys::ComponentContext* component_context,
              std::unique_ptr<coroutine::CoroutineService> coroutine_service,
              BackoffFactory backoff_factory,
              std::unique_ptr<timekeeper::Clock> clock,
              std::unique_ptr<rng::Random> random);
  Environment(Environment&& other) noexcept;
  ~Environment();

  Environment& operator=(Environment&& other) noexcept;

  bool disable_statistics() const { return disable_statistics_; }

  async_dispatcher_t* dispatcher() const { return dispatcher_; }

  // Returns the async_dispatcher_t to be used for I/O operations.
  async_dispatcher_t* io_dispatcher() const { return io_dispatcher_; }

  const std::string& firebase_api_key() const { return firebase_api_key_; };

  sys::ComponentContext* component_context() const {
    return component_context_;
  };

  coroutine::CoroutineService* coroutine_service() const {
    return coroutine_service_.get();
  }

  std::unique_ptr<backoff::Backoff> MakeBackoff();

  timekeeper::Clock* clock() const { return clock_.get(); }

  rng::Random* random() const { return random_.get(); }

 private:
  bool disable_statistics_;

  async_dispatcher_t* dispatcher_;

  // The async_dispatcher_t to be used for I/O operations.
  async_dispatcher_t* io_dispatcher_;

  // The firebase API key.
  std::string firebase_api_key_;

  sys::ComponentContext* component_context_;
  std::unique_ptr<coroutine::CoroutineService> coroutine_service_;
  BackoffFactory backoff_factory_;
  std::unique_ptr<timekeeper::Clock> clock_;
  std::unique_ptr<rng::Random> random_;
};

// Builder for the environment.
//
// The |SetAsync|, |SetIOAsync| and |SetStartupContext| methods must be called
// before the environment can be built.
class EnvironmentBuilder {
 public:
  EnvironmentBuilder();
  ~EnvironmentBuilder();

  EnvironmentBuilder(const EnvironmentBuilder& other) = delete;
  EnvironmentBuilder(EnvironmentBuilder&& other) = delete;
  EnvironmentBuilder& operator=(const EnvironmentBuilder& other) = delete;
  EnvironmentBuilder& operator=(EnvironmentBuilder&& other) = delete;

  EnvironmentBuilder& SetDisableStatistics(bool disable_statistics);
  EnvironmentBuilder& SetAsync(async_dispatcher_t* dispatcher);
  EnvironmentBuilder& SetIOAsync(async_dispatcher_t* io_dispatcher);
  EnvironmentBuilder& SetFirebaseApiKey(std::string firebase_api_key);
  EnvironmentBuilder& SetStartupContext(
      sys::ComponentContext* component_context);
  EnvironmentBuilder& SetCoroutineService(
      std::unique_ptr<coroutine::CoroutineService> coroutine_service);
  EnvironmentBuilder& SetBackoffFactory(
      Environment::BackoffFactory backoff_factory);
  EnvironmentBuilder& SetClock(std::unique_ptr<timekeeper::Clock> clock);
  EnvironmentBuilder& SetRandom(std::unique_ptr<rng::Random> random);

  Environment Build();

 private:
  bool disable_statistics_ = true;
  async_dispatcher_t* dispatcher_ = nullptr;
  async_dispatcher_t* io_dispatcher_ = nullptr;
  std::string firebase_api_key_;
  sys::ComponentContext* component_context_ = nullptr;
  std::unique_ptr<coroutine::CoroutineService> coroutine_service_;
  Environment::BackoffFactory backoff_factory_;
  std::unique_ptr<timekeeper::Clock> clock_;
  std::unique_ptr<rng::Random> random_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_ENVIRONMENT_ENVIRONMENT_H_
