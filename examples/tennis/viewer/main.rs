// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_game_tennis::{GameState, TennisServiceMarker};
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon::DurationNum;
use std::io;
use std::io::Write;

const DRAW_WIDTH: usize = 80;
const DRAW_HEIGHT: usize = 25;

const BOARD_WIDTH: f64 = 20.0;
const BOARD_HEIGHT: f64 = 10.0;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let tennis_service = connect_to_service::<TennisServiceMarker>()?;

    let mut first_print = true;

    println!("connected to tennis service");
    let resp: Result<(), Error> = executor.run_singlethreaded(async move {
        loop {
            let time_step: i64 = 1000 / 20;
            fuchsia_async::Timer::new(time_step.millis().after_now()).await;

            let state = tennis_service.get_state().await?;
            if state.game_num == 0 {
                continue;
            }

            if first_print {
                first_print = false;
            } else {
                // Print the following to stdout:
                // - ESC
                // - [
                // - The number of lines to move the cursor up, in asci
                // - A
                // This is using the ECMA-48 CSI sequences as described here:
                // http://man7.org/linux/man-pages/man4/console_codes.4.html
                let mut to_print = Vec::new();
                to_print.push(0x1B);
                to_print.push(0x5B);
                to_print.append(&mut format!("{}", DRAW_HEIGHT).into_bytes().to_vec());
                to_print.push(0x46);

                io::stdout().write(&to_print)?;
            }

            print_game(state);
        }
    });
    resp
}

fn print_game(state: GameState) {
    let banner_height = 3;
    let board_draw_height = DRAW_HEIGHT - banner_height;

    let paddle_1_loc = ((state.player_1_y / BOARD_HEIGHT) * (board_draw_height as f64)) as usize;
    let paddle_2_loc = ((state.player_2_y / BOARD_HEIGHT) * (board_draw_height as f64)) as usize;

    let ball_x_loc = (state.ball_x / BOARD_WIDTH * ((DRAW_WIDTH - 1) as f64)) as usize;
    let ball_y_loc = (state.ball_y / BOARD_HEIGHT * ((board_draw_height - 1) as f64)) as usize;

    let mut output = "".to_string();
    output.push_str(&state.player_1_name);
    output
        .push_str(&" ".repeat(DRAW_WIDTH - state.player_1_name.len() - state.player_2_name.len()));
    output.push_str(&state.player_2_name);
    output.push_str("\n");

    let p1_score = format!("{}", state.player_1_score);
    let p2_score = format!("{}", state.player_2_score);
    output.push_str(&p1_score);
    output.push_str(&" ".repeat(DRAW_WIDTH - p1_score.len() - p2_score.len()));
    output.push_str(&p2_score);
    output.push_str("\n");

    for y in 0..board_draw_height {
        for x in 0..DRAW_WIDTH {
            // I have no clue why this "as usize" is necessary
            if (x, y) == (ball_x_loc as usize, ball_y_loc) {
                output.push_str("0");
            } else if (x, y) == (0, paddle_1_loc) {
                output.push_str(")");
            } else if (x, y) == (DRAW_WIDTH - 1, paddle_2_loc) {
                output.push_str("(");
            } else if x == DRAW_WIDTH / 2 {
                output.push_str("|");
            } else {
                output.push_str(" ");
            }
        }
        output.push_str("\n");
    }
    println!("{}", output);
}
