// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "promise_example2.h"

#include <algorithm>
#include <memory>
#include <string>

#include <lib/fit/promise.h>
#include <lib/fit/single_threaded_executor.h>

#include "utils.h"

namespace promise_example2 {

// State for a two player game.
//
// Players do battle by simultaneously rolling dice in order to inflict
// damage upon their opponent over the course of several rounds until one
// or both players' hit points are depleted to 0.
//
// Players start with 100 hit points.  During each round, each player first
// rolls a Damage die (numbered 0 to 9) and an Effect die (numbered 0 to 3).
// If the Effect die comes up 0, the player casts a lightning spell and
// rolls an Effect Multiplier die (numbered 0 to 4) to determine the
// strength of the effect.
//
// The following calculation determines the damage dealt to the player's
// opponent:
//
//   if Damage die value is non-zero,
//     then opponent HP -= value of Damage die
//   if Effect die is zero (cast lighting),
//     then opponent HP -= value of Effect Multiplier die * 2 + 3
//
// Any dice that fly off the table during especially vigorous rolls are
// rerolled before damage is tallied.
struct game_state {
  int red_hp = 100;
  int blue_hp = 100;
};

// Rolls a die and waits for it to settle down then returns its value.
// This task might fail so the caller needs to be prepared to re-roll.
//
// This function demonstrates returning pending, error, and ok states as well
// as task suspension.
auto roll_die(std::string player, std::string type, int number_of_sides) {
  return fit::make_promise(
      [player, type, number_of_sides](fit::context& context) -> fit::result<int> {
        // Simulate the outcome of rolling a die.
        // Either the die will settle, keep rolling, or fall off the table.
        int event = rand() % 6;
        if (event == 0) {
          // The die flew off the table!
          printf("    %s's '%s' die flew right off the table!\n", player.c_str(), type.c_str());
          return fit::error();
        }
        if (event < 3) {
          // The die is still rolling around.  Need to wait for it to settle.
          utils::resume_in_a_little_while(context.suspend_task());
          return fit::pending();
        }
        // The die has finished rolling, determine how it landed.
        int value = rand() % number_of_sides;
        printf("    %s rolled %d for '%s'\n", player.c_str(), value, type.c_str());
        return fit::ok(value);
      });
}

// Re-rolls a die until it succeeds.
//
// This function demonstrates looping a task using a recursive tail-call.
fit::promise<int> roll_die_until_successful(std::string player, std::string type,
                                            int number_of_sides) {
  return roll_die(player, type, number_of_sides).or_else([player, type, number_of_sides] {
    // An error occurred while rolling the die.  Recurse to try again.
    return roll_die_until_successful(player, type, number_of_sides);
  });
}

// Rolls an effect and damage die.
// If the effect die comes up 0 then also rolls an effect multiplier die to
// determine the strength of the effect.  We can do this while waiting
// for the damage die to settle down.
//
// This functions demonstrates the benefits of capturing a task into a
// |fit::future| so that its result can be retained and repeatedly
// examined while awaiting other tasks.
auto roll_for_damage(std::string player) {
  return fit::make_promise(
      [player, damage = fit::future<int>(roll_die_until_successful(player, "damage", 10)),
       effect = fit::future<int>(roll_die_until_successful(player, "effect", 4)),
       effect_multiplier = fit::future<int>()](fit::context& context) mutable -> fit::result<int> {
        // Evaluate the damage die roll future.
        bool damage_ready = damage(context);

        // Evaluate the effect die roll future.
        // If the player rolled lightning, begin rolling the multiplier.
        bool effect_ready = effect(context);
        if (effect_ready) {
          if (effect.value() == 0) {
            if (!effect_multiplier)
              effect_multiplier = roll_die_until_successful(player, "multiplier", 4);
            effect_ready = effect_multiplier(context);
          }
        }

        // If we're still waiting for the dice to settle, return pending.
        // The task will be resumed once it can make progress.
        if (!effect_ready || !damage_ready)
          return fit::pending();

        // Calculate the result and describe what happened.
        if (damage.value() == 0)
          printf("%s swings wildly and completely misses their opponent\n", player.c_str());
        else
          printf("%s hits their opponent for %d damage\n", player.c_str(), damage.value());

        int effect_bonus = 0;
        if (effect.value() == 0) {
          if (effect_bonus == 0) {
            printf(
                "%s attempts to cast 'lightning' but the spell "
                "fizzles without effect\n",
                player.c_str());
          } else {
            effect_bonus = effect_multiplier.value() * 2 + 3;
            printf("%s casts 'lightning' for %d damage\n", player.c_str(), effect_bonus);
          }
        }

        return fit::ok(damage.value() + effect_bonus);
      });
}

// Plays one round of the game.
// Both players roll dice simultaneously to determine the damage dealt
// to their opponent.
//
// This function demonstrates joining the results of concurrently executed
// tasks as a new task which produces a tuple.
auto play_round(const std::shared_ptr<game_state>& state) {
  return fit::join_promises(roll_for_damage("Red"), roll_for_damage("Blue"))
      .and_then([state](const std::tuple<fit::result<int>, fit::result<int>>& damages) {
        // Damage tallies are ready, apply them to the game state.
        state->blue_hp = std::max(state->blue_hp - std::get<0>(damages).value(), 0);
        state->red_hp = std::max(state->red_hp - std::get<1>(damages).value(), 0);
        printf("Hit-points remaining: red %d, blue %d\n", state->red_hp, state->blue_hp);
      });
}

// Plays a little game.
// Red and Blue each start with 100 hit points.
// During each round, they both simultaneously roll dice to determine damage to
// their opponent.  If at the end of the round one player's hit-points reaches
// 0, that player loses.  If both players' hit-points reach 0, they both lose.
auto play_game() {
  puts("Red and Blue are playing a game...");
  return fit::make_promise([state = std::make_shared<game_state>(), round = fit::future<>()](
                               fit::context& context) mutable -> fit::result<> {
    // Repeatedly play rounds until the game ends.
    while (state->red_hp != 0 && state->blue_hp != 0) {
      if (!round)
        round = play_round(state);
      if (!round(context))
        return fit::pending();
      round = nullptr;
    }

    // Game over.
    puts("Game over...");
    if (state->red_hp == 0 && state->blue_hp == 0)
      puts("Both players lose!");
    else if (state->red_hp != 0)
      puts("Red wins!");
    else
      puts("Blue wins!");
    return fit::ok();
  });
}

void run() { fit::run_single_threaded(play_game()); }

}  // namespace promise_example2
