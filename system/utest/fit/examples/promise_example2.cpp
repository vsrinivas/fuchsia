// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "promise_example2.h"

#include <algorithm>
#include <string>

#include <lib/fit/promise.h>
#include <lib/fit/sequential_executor.h>

#include "utils.h"

namespace promise_example2 {

// Rolls a die and waits for it to settle down then returns its value.
// This task might fail so the caller needs to be prepared to re-roll.
//
// This function demonstrates returning pending, error, and ok states as well
// as task suspension.
auto roll_die(std::string player, std::string type, int number_of_sides) {
    return fit::make_promise([player, type, number_of_sides](fit::context& context)
                                 -> fit::result<int> {
        int event = rand() % 6;
        if (event == 0) {
            // Imagine that the die flew off the table!
            printf("    %s's '%s' die flew right off the table!\n",
                   player.c_str(), type.c_str());
            return fit::error();
        }
        if (event < 3) {
            // Imagine that the die is still rolling around.
            utils::resume_in_a_little_while(context.suspend_task());
            return fit::pending();
        }
        // Imagine that the die has finished rolling.
        int value = rand() % number_of_sides;
        printf("    %s rolled %d for '%s'\n", player.c_str(), value, type.c_str());
        return fit::ok(value);
    });
}

// Re-rolls a die until it succeeds.
//
// This function demonstrates looping a task using a recursive tail-call.
fit::promise<int> roll_die_until_successful(
    std::string player, std::string type, int number_of_sides) {
    return roll_die(player, type, number_of_sides)
        .or_else([player, type, number_of_sides] {
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
        [player,
         damage = fit::future<int>(roll_die_until_successful(player, "damage", 10)),
         effect = fit::future<int>(roll_die_until_successful(player, "effect", 4)),
         effect_multiplier = fit::future<int>()](fit::context& context) mutable
        -> fit::result<int> {
            bool damage_ready = damage(context);

            bool effect_ready = effect(context);
            if (effect_ready) {
                if (effect.value() == 0) {
                    if (!effect_multiplier)
                        effect_multiplier = roll_die_until_successful(player, "multiplier", 4);
                    effect_ready = effect_multiplier(context);
                }
            }

            if (!effect_ready || !damage_ready)
                return fit::pending();

            if (damage.value() == 0)
                printf("%s swings wildly and completely misses their opponent\n",
                       player.c_str());
            else
                printf("%s hits their opponent for %d damage\n",
                       player.c_str(), damage.value());

            int effect_bonus = 0;
            if (effect.value() == 0) {
                if (effect_bonus == 0) {
                    printf("%s attempts to cast 'lightning' but the spell "
                           "fizzles without effect\n",
                           player.c_str());
                } else {
                    effect_bonus = effect_multiplier.value() * 2 + 3;
                    printf("%s casts 'lightning' for %d damage\n",
                           player.c_str(), effect_bonus);
                }
            }
            return fit::ok(damage.value() + effect_bonus);
        });
}

// Plays one round of the game.
// Both players roll dice simultaneously to determine the damage dealt
// to their opponent.  Returns true if the game is over.
//
// This function demonstrates joining the results of concurrently executed
// tasks as a new task which produces a tuple.
auto play_round(int* red_hp, int* blue_hp) {
    return fit::join_promises(roll_for_damage("Red"), roll_for_damage("Blue"))
        .and_then(
            [red_hp, blue_hp](
                std::tuple<fit::result<int>, fit::result<int>> damages) mutable
            -> fit::result<bool> {
                *blue_hp = std::max(*blue_hp - std::get<0>(damages).value(), 0);
                *red_hp = std::max(*red_hp - std::get<1>(damages).value(), 0);
                printf("Hit-points remaining: red %d, blue %d\n", *red_hp, *blue_hp);
                if (*red_hp != 0 && *blue_hp != 0)
                    return fit::ok(false);

                // Game over.
                puts("Game over...");
                if (*red_hp == 0 && *blue_hp == 0)
                    puts("Both players lose!");
                else if (*red_hp != 0)
                    puts("Red wins!");
                else
                    puts("Blue wins!");
                return fit::ok(true);
            });
}

// Plays a little game.
// Red and Blue each start with 100 hit points.
// During each round, they both simultaneously roll dice to determine damage to
// their opponent.  If at the end of the round one player's hit-points reaches
// 0, that player loses.  If both players' hit-points reach 0, they both lose.
auto play_game() {
    return fit::make_promise([red_hp = 100, blue_hp = 100](fit::context& context) mutable {
        puts("Red and Blue are playing a game...");

        // TODO: We might benefit from some kind of loop combinator here.
        return fit::make_promise(
            [&red_hp, &blue_hp,
             round = fit::future<bool>()](fit::context& context) mutable
            -> fit::result<> {
                for (;;) {
                    if (!round)
                        round = play_round(&red_hp, &blue_hp);
                    if (!round(context))
                        return fit::pending();

                    bool game_over = round.value();
                    if (game_over)
                        return fit::ok();
                    round = nullptr;
                }
            });
    });
}

void run() {
    fit::run_sequentially(play_game());
}

} // namespace promise_example2
