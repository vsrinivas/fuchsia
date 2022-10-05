// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/future.h>
#include <lib/fasync/single_threaded_executor.h>

#include <random>
#include <string>

#include <zxtest/zxtest.h>

// This example demonstrates sequencing of tasks using combinators.
namespace {

void resume_in_a_little_while(fasync::suspended_task task) {
  std::thread([task]() mutable {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.resume();
  }).detach();
}

template <typename T>
T random(T min, cpp20::type_identity_t<T> max) {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<T> dist(min, max);
  return dist(gen);
}

fasync::try_future<std::string, int> pick_bananas(int hours) {
  return fasync::make_future(
      [hours, time = 0,
       harvest = 0](fasync::context& context) mutable -> fasync::try_poll<std::string, int> {
        if (time == 0) {
          printf("Starting the day picking bananas for %d hours...\n", hours);
        } else {
          printf("... %d hour elapsed...\n", time);
        }
        if (random(0, 6) == 0) {
          return fasync::ready(fit::error("A wild animal ate all the bananas we picked today!"));
        }
        if (time < hours) {
          // Simulate time passing.
          // Here we call |suspend_task()| to obtain a |fasync::suspended_task|
          // which acts as a handle which will later be used by
          // |resume_in_a_little_while()| to resume the task.  In the
          // meantime, we unwind the call stack by returning |fasync::pending()|.
          // Once the task is resumed, the future's handler will restart
          // execution from the top again, however it will have retained
          // state (in |time| and |harvest|) from its prior execution.
          resume_in_a_little_while(context.suspend_task());
          time++;
          harvest += random(0, 30);
          return fasync::pending();
        }
        return fasync::ready(fit::ok(harvest));
      });
}

fasync::try_future<std::string> eat_bananas(int appetite) {
  return fasync::make_future(
      [appetite](fasync::context& context) mutable -> fasync::try_poll<std::string> {
        if (appetite > 0) {
          printf("... eating a yummy banana....\n");
          resume_in_a_little_while(context.suspend_task());
          appetite--;
          if (random(0, 10) == 0) {
            return fasync::ready(fit::error("I ate too many bananas. Urp."));
          }
          return fasync::pending();
        }
        puts("Ahh. So satisfying.");
        return fasync::ready(fit::ok());
      });
}

fasync::try_future<fit::failed> prepare_simulation() {
  int hours = random(0, 7);
  return pick_bananas(hours) |
         fasync::and_then([](const int& harvest) -> fit::result<std::string, int> {
           printf("We picked %d bananas today!\n", harvest);
           if (harvest == 0)
             return fit::error("What will we eat now?");
           return fit::ok(harvest);
         }) |
         fasync::and_then([](const int& harvest) {
           int appetite = random(0, 6);
           if (appetite > harvest)
             appetite = harvest;
           return eat_bananas(appetite);
         }) |
         fasync::or_else([](const std::string& error) {
           printf("Oh no! %s\n", error.c_str());
           return fit::failed();
         }) |
         fasync::and_then([] { puts("*** Simulation finished ***"); }) | fasync::or_else([] {
           puts("*** Restarting simulation ***");
           return prepare_simulation();
         });
}

TEST(FutureTests, SimulationExample) {
  auto simulation = prepare_simulation();
  [[maybe_unused]] auto result = fasync::block(std::move(simulation));
}

}  // namespace
