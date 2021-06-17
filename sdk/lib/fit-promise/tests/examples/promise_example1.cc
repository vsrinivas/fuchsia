// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "promise_example1.h"

#include <lib/fpromise/promise.h>
#include <lib/fpromise/single_threaded_executor.h>

#include <string>

#include "utils.h"

// This example demonstrates sequencing of tasks using combinators.
namespace promise_example1 {

fpromise::promise<int, std::string> pick_bananas(int hours) {
  return fpromise::make_promise(
      [hours, time = 0,
       harvest = 0](fpromise::context& context) mutable -> fpromise::result<int, std::string> {
        if (time == 0) {
          printf("Starting the day picking bananas for %d hours...\n", hours);
        } else {
          printf("... %d hour elapsed...\n", time);
        }
        if (random() % 7 == 0) {
          return fpromise::error("A wild animal ate all the bananas we picked today!");
        }
        if (time < hours) {
          // Simulate time passing.
          // Here we call |suspend_task()| to obtain a |fpromise::suspended_task|
          // which acts as a handle which will later be used by
          // |resume_in_a_little_while()| to resume the task.  In the
          // meantime, we unwind the call stack by returning |fpromise::pending()|.
          // Once the task is resumed, the promise's handler will restart
          // execution from the top again, however it will have retained
          // state (in |time| and |harvest|) from its prior execution.
          utils::resume_in_a_little_while(context.suspend_task());
          time++;
          harvest += static_cast<int>(random() % 31);
          return fpromise::pending();
        }
        return fpromise::ok(harvest);
      });
}

fpromise::promise<void, std::string> eat_bananas(int appetite) {
  return fpromise::make_promise(
      [appetite](fpromise::context& context) mutable -> fpromise::result<void, std::string> {
        if (appetite > 0) {
          printf("... eating a yummy banana....\n");
          utils::resume_in_a_little_while(context.suspend_task());
          appetite--;
          if (random() % 11 == 0) {
            return fpromise::error("I ate too many bananas.  Urp.");
          }
          return fpromise::pending();
        }
        puts("Ahh.  So satisfying.");
        return fpromise::ok();
      });
}

fpromise::promise<> prepare_simulation() {
  int hours = static_cast<int>(random() % 8);
  return pick_bananas(hours)
      .and_then([](const int& harvest) -> fpromise::result<int, std::string> {
        printf("We picked %d bananas today!\n", harvest);
        if (harvest == 0)
          return fpromise::error("What will we eat now?");
        return fpromise::ok(harvest);
      })
      .and_then([](const int& harvest) {
        int appetite = static_cast<int>(random() % 7);
        if (appetite > harvest)
          appetite = harvest;
        return eat_bananas(appetite);
      })
      .or_else([](const std::string& error) {
        printf("Oh no!  %s\n", error.c_str());
        return fpromise::error();
      })
      .and_then([] { puts("*** Simulation finished ***"); })
      .or_else([] {
        puts("*** Restarting simulation ***");
        return prepare_simulation();
      });
}

void run() {
  auto simulation = prepare_simulation();
  fpromise::run_single_threaded(std::move(simulation));
}

}  // namespace promise_example1
