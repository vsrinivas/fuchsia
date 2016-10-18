// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/interfaces/debug.mojom.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

typedef std::function<void(mojo::Shell*)> TestRoutine;

struct Test {
  Test(const std::string& name, TestRoutine run) : name(name), run(run) {}
  const std::string name;
  const TestRoutine run;
};

void Yield();

template <typename Predicate>
void WaitUntil(Predicate until) {
  do {
    Yield();
  } while (!until());
}

using Predicate = std::function<bool()>;

Predicate operator&&(const Predicate& a, const Predicate& b);
Predicate operator||(const Predicate& a, const Predicate& b);
Predicate operator!(const Predicate& a);

// Converts a closure to a tautology. This is useful in combination with the
// Boolean operators above to add side effects to predicates, for example
// fataling on a deadline.
template <typename Closure>
Predicate SideEffect(Closure side_effect) {
  return [&side_effect] {
    side_effect();
    return true;
  };
}

#define PREDICATE(condition) [&] { return condition; }

Predicate Deadline(unsigned int millis);

void StartComponent(mojo::Shell* shell, const std::string& url);
void Sleep(unsigned int millis);

constexpr MojoTimeTicks kPauseIdleMs = 250;
constexpr MojoTimeTicks kPauseMaxMs = 2000;

// Pauses the main thread to allow Mojo messages to propagate. It will wait
// until no new debuggable have been launched for 500 ms, for a max of 2
// seconds.
void Pause();

// In practice, 100 ms is actually a bit short, so this may occasionally falsely
// succeed tests that should fail. Flakiness should thus be considered failure.
constexpr MojoTimeTicks kAsyncCheckSteadyMs = 100;

// Does a weak stability check on an async condition by waiting until the given
// condition is true (max 2s) and then ensuring that the condition remains true
// (for 100 ms).
//
// If the condition becomes true briefly but not over a 100 ms polling period,
// this check continues waiting until the deadline. Since the transient check
// is polling-based, the exact number of matches should not be relied upon.
#define ASYNC_CHECK(condition)                                             \
  {                                                                        \
    auto deadline = Deadline(kPauseMaxMs);                                 \
    auto check = PREDICATE(condition);                                     \
    do {                                                                   \
      WaitUntil(check ||                                                   \
                deadline && SideEffect([] {                                \
                  MOJO_LOG(FATAL)                                          \
                      << "Deadline exceeded for async check: " #condition; \
                }));                                                       \
      auto steady = Deadline(kAsyncCheckSteadyMs);                         \
      WaitUntil(steady || !check);                                         \
    } while (!(condition));                                                \
  }
