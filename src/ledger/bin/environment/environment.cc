// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/environment/environment.h"

#include "src/ledger/bin/environment/thread_notification.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/backoff/exponential_backoff.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/rng/random.h"
#include "src/ledger/lib/rng/system_random.h"
#include "src/ledger/lib/timekeeper/system_clock.h"

namespace ledger {

Environment::Environment(std::unique_ptr<Platform> platform, bool disable_statistics,
                         async_dispatcher_t* dispatcher, async_dispatcher_t* io_dispatcher,
                         sys::ComponentContext* component_context,
                         std::unique_ptr<coroutine::CoroutineService> coroutine_service,
                         BackoffFactory backoff_factory, NotificationFactory notification_factory,
                         std::unique_ptr<Clock> clock, std::unique_ptr<Random> random,
                         storage::GarbageCollectionPolicy gc_policy,
                         storage::DiffCompatibilityPolicy diff_compatibility_policy)
    : platform_(std::move(platform)),
      disable_statistics_(disable_statistics),
      dispatcher_(dispatcher),
      io_dispatcher_(io_dispatcher),
      component_context_(component_context),
      coroutine_service_(std::move(coroutine_service)),
      backoff_factory_(std::move(backoff_factory)),
      notification_factory_(std::move(notification_factory)),
      clock_(std::move(clock)),
      random_(std::move(random)),
      gc_policy_(gc_policy),
      diff_compatibility_policy_(diff_compatibility_policy) {
  LEDGER_DCHECK(dispatcher_);
  LEDGER_DCHECK(io_dispatcher_);
  LEDGER_DCHECK(dispatcher_ != io_dispatcher_);
  LEDGER_DCHECK(component_context_);
  LEDGER_DCHECK(coroutine_service_);
  LEDGER_DCHECK(backoff_factory_);
  LEDGER_DCHECK(notification_factory_);
  LEDGER_DCHECK(clock_);
  LEDGER_DCHECK(random_);
}

Environment::Environment(Environment&& other) noexcept { *this = std::move(other); }

Environment& Environment::operator=(Environment&& other) noexcept = default;

Environment::~Environment() = default;

std::unique_ptr<Backoff> Environment::MakeBackoff() { return backoff_factory_(); }

std::unique_ptr<Notification> Environment::MakeNotification() { return notification_factory_(); }

EnvironmentBuilder::EnvironmentBuilder() = default;

EnvironmentBuilder::~EnvironmentBuilder() = default;

EnvironmentBuilder& EnvironmentBuilder::SetPlatform(std::unique_ptr<Platform> platform) {
  platform_ = std::move(platform);
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetDisableStatistics(bool disable_statistics) {
  disable_statistics_ = disable_statistics;
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetAsync(async_dispatcher_t* dispatcher) {
  dispatcher_ = dispatcher;
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetIOAsync(async_dispatcher_t* io_dispatcher) {
  io_dispatcher_ = io_dispatcher;
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

EnvironmentBuilder& EnvironmentBuilder::SetBackoffFactory(
    Environment::BackoffFactory backoff_factory) {
  backoff_factory_ = std::move(backoff_factory);
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetNotificationFactory(
    Environment::NotificationFactory notification_factory) {
  notification_factory_ = std::move(notification_factory);
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetClock(std::unique_ptr<Clock> clock) {
  clock_ = std::move(clock);
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetRandom(std::unique_ptr<Random> random) {
  random_ = std::move(random);
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetGcPolicy(storage::GarbageCollectionPolicy gc_policy) {
  gc_policy_ = gc_policy;
  return *this;
}

EnvironmentBuilder& EnvironmentBuilder::SetDiffCompatibilityPolicy(
    storage::DiffCompatibilityPolicy diff_compatibility_policy) {
  diff_compatibility_policy_ = diff_compatibility_policy;
  return *this;
}

Environment EnvironmentBuilder::Build() {
  if (!platform_) {
    platform_ = MakePlatform();
  }
  if (!coroutine_service_) {
    coroutine_service_ = std::make_unique<coroutine::CoroutineServiceImpl>();
  }
  if (!clock_) {
    clock_ = std::make_unique<SystemClock>();
  }
  if (!random_) {
    random_ = std::make_unique<SystemRandom>();
  }
  if (!backoff_factory_) {
    backoff_factory_ = [random = random_.get()] {
      return std::make_unique<ExponentialBackoff>(random->NewBitGenerator<uint64_t>());
    };
  }
  if (!notification_factory_) {
    notification_factory_ = [] { return std::make_unique<ThreadNotification>(); };
  }
  return Environment(std::move(platform_), disable_statistics_, dispatcher_, io_dispatcher_,
                     component_context_, std::move(coroutine_service_), std::move(backoff_factory_),
                     std::move(notification_factory_), std::move(clock_), std::move(random_),
                     gc_policy_, diff_compatibility_policy_);
}

}  // namespace ledger
