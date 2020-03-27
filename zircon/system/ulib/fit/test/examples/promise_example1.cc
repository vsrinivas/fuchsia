// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "promise_example1.h"

#include <string>

#include <lib/fit/promise.h>
#include <lib/fit/single_threaded_executor.h>

#include "utils.h"

// This example demonstrates sequencing of tasks using combinators.
namespace promise_example1 {

fit::promise<int, std::string> pick_bananas(int hours) {
  return fit::make_promise([hours, time = 0, harvest = 0](
                               fit::context& context) mutable -> fit::result<int, std::string> {
    if (time == 0) {
      printf("Starting the day picking bananas for %d hours...\n", hours);
    } else {
      printf("... %d hour elapsed...\n", time);
    }
    if (random() % 7 == 0) {
      return fit::error("A wild animal ate all the bananas we picked today!");
    }
    if (time < hours) {
      // Simulate time passing.
      // Here we call |suspend_task()| to obtain a |fit::suspended_task|
      // which acts as a handle which will later be used by
      // |resume_in_a_little_while()| to resume the task.  In the
      // meantime, we unwind the call stack by returning |fit::pending()|.
      // Once the task is resumed, the promise's handler will restart
      // execution from the top again, however it will have retained
      // state (in |time| and |harvest|) from its prior execution.
      utils::resume_in_a_little_while(context.suspend_task());
      time++;
      harvest += static_cast<int>(random() % 31);
      return fit::pending();
    }
    return fit::ok(harvest);
  });
}

fit::promise<void, std::string> eat_bananas(int appetite) {
  return fit::make_promise(
      [appetite](fit::context& context) mutable -> fit::result<void, std::string> {
        if (appetite > 0) {
          printf("... eating a yummy banana....\n");
          utils::resume_in_a_little_while(context.suspend_task());
          appetite--;
          if (random() % 11 == 0) {
            return fit::error("I ate too many bananas.  Urp.");
          }
          return fit::pending();
        }
        puts("Ahh.  So satisfying.");
        return fit::ok();
      });
}

fit::promise<> prepare_simulation() {
  int hours = static_cast<int>(random() % 8);
  return pick_bananas(hours)
      .and_then([](const int& harvest) -> fit::result<int, std::string> {
        printf("We picked %d bananas today!\n", harvest);
        if (harvest == 0)
          return fit::error("What will we eat now?");
        return fit::ok(harvest);
      })
      .and_then([](const int& harvest) {
        int appetite = static_cast<int>(random() % 7);
        if (appetite > harvest)
          appetite = harvest;
        return eat_bananas(appetite);
      })
      .or_else([](const std::string& error) {
        printf("Oh no!  %s\n", error.c_str());
        return fit::error();
      })
      .and_then([] { puts("*** Simulation finished ***"); })
      .or_else([] {
        puts("*** Restarting simulation ***");
        return prepare_simulation();
      });
}

void run() {
  auto simulation = prepare_simulation();
  fit::run_single_threaded(std::move(simulation));
}

}  // namespace promise_example1
