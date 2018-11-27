// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::f64::consts::PI;
use fidl_fuchsia_game_tennis as fidl_tennis;
use fidl_fuchsia_game_tennis::{GameState, PaddleProxy};
use fuchsia_syslog::fx_log_info;
use futures::prelude::*;
use parking_lot::Mutex;
use rand::random;
use std::f64;
use std::sync::{Arc, Weak};

const TIME_SCALE_FACTOR: f64 = 0.02; // Time scale factor: scales all movements/speeds by this amount.
const BOARD_HEIGHT: f64 = 10.0;
const BOARD_WIDTH: f64 = 20.0;
const PADDLE_SPEED: f64 = 0.4 * TIME_SCALE_FACTOR; // distance paddle travels per step
const PADDLE_SIZE: f64 = 1.0; // vertical height of paddle
const PADDLE_MAX_ANGLE: f64 = std::f64::consts::PI / 4.0; // Maximum paddle angle
const BALL_SPEEDUP_MULTIPLIER: f64 = 1.05; // speed multiplier applied on every paddle bounce
const MAX_BOUNCE_ANGLE: f64 = 1.3; // in radians, bounce angle when hitting very top edge of paddle

pub struct Game {
    state: GameState,
    player_1: Option<Player>,
    player_2: Option<Player>,
    ball_dx: f64,
    ball_dy: f64,
}

#[derive(Clone)]
struct Player {
    pub name: String,
    pub state: Arc<Mutex<PlayerState>>,
    pub proxy: PaddleProxy,
}

#[derive(Clone)]
pub enum PlayerState {
    Up,
    Down,
    Stop,
    Disconnected,
}

fn calc_paddle_movement(pos: &mut f64, state: &PlayerState) -> bool {
    let player_delta = match state {
        PlayerState::Up => PADDLE_SPEED * -1.0,
        PlayerState::Down => PADDLE_SPEED,
        PlayerState::Stop => 0.0,
        PlayerState::Disconnected => return false,
    };
    let mut new_paddle_location = *pos + player_delta;
    new_paddle_location = new_paddle_location.max(PADDLE_SIZE / 2.0);
    new_paddle_location = new_paddle_location.min(BOARD_HEIGHT - PADDLE_SIZE / 2.0);
    *pos = new_paddle_location;
    true
}

fn calc_paddle_bounce(dx: &mut f64, dy: &mut f64, ball_y: f64, paddle_y: f64) {
    let speed = ((*dx) * (*dx) + (*dy) * (*dy)).sqrt() * BALL_SPEEDUP_MULTIPLIER;
    let paddle_angle = PADDLE_MAX_ANGLE * (ball_y - paddle_y) / (PADDLE_SIZE / 2.0);

    let new_ball_dx = -dx.signum() * speed * paddle_angle.cos();
    let new_ball_dy = speed * paddle_angle.sin();
    if (*dx) * new_ball_dx >= 0.0 {
        panic!("Ball bouncing in the same direction!");
    }
    *dx = new_ball_dx;
    *dy = new_ball_dy;
}

impl Game {
    /// return clone of internal state
    pub fn state(&self) -> GameState {
        fidl_tennis::GameState {
            ball_x: self.state.ball_x,
            ball_y: self.state.ball_y,
            player_1_y: self.state.player_1_y,
            player_2_y: self.state.player_2_y,
            player_1_score: self.state.player_1_score,
            player_2_score: self.state.player_2_score,
            player_1_name: self.state.player_1_name.clone(),
            player_2_name: self.state.player_2_name.clone(),
            time: self.state.time,
            game_num: self.state.game_num,
        }
    }
    pub fn new() -> Game {
        Game {
            player_1: None,
            player_2: None,
            ball_dx: 0.0,
            ball_dy: 0.0,
            state: GameState {
                ball_x: 0.0,
                ball_y: 0.0,
                game_num: 0,
                player_1_y: 0.0,
                player_2_y: 0.0,
                player_1_score: 0,
                player_2_score: 0,
                player_1_name: "".to_string(),
                player_2_name: "".to_string(),
                time: 0,
            },
        }
    }

    pub fn time_scale_factor(&self) -> f64 {
        TIME_SCALE_FACTOR
    }

    pub fn players_ready(&self) -> bool {
        return self.player_1.is_some() && self.player_2.is_some();
    }

    pub fn register_new_paddle(
        &mut self, player_name: String, paddle_proxy: PaddleProxy,
    ) -> Arc<Mutex<PlayerState>> {
        let paddle = Player {
            name: player_name.clone(),
            state: Arc::new(Mutex::new(PlayerState::Stop)),
            proxy: paddle_proxy,
        };
        let res = paddle.state.clone();
        if self.player_1.is_none() {
            self.player_1 = Some(paddle);
            self.state.player_1_name = player_name;
        } else if self.player_2.is_none() {
            self.player_2 = Some(paddle);
            self.state.player_2_name = player_name;
        } else {
            panic!("too many clients connected");
        }
        return res;
    }

    pub fn step(&mut self) {
        if self.players_ready() && self.state.game_num == 0 {
            self.new_game();
        } else if !self.players_ready() {
            // game has not started yet
            return;
        }

        self.state.time += 1;

        if !calc_paddle_movement(
            &mut self.state.player_1_y,
            &self.player_1.as_mut().unwrap().state.lock(),
        ) {
            self.player_1 = None;
            self.new_game();
            self.state.game_num = 0;
            self.state.player_1_score = 0;
            self.state.player_2_score = 0;
            return;
        }
        if !calc_paddle_movement(
            &mut self.state.player_2_y,
            &self.player_2.as_mut().unwrap().state.lock(),
        ) {
            self.player_2 = None;
            self.new_game();
            self.state.game_num = 0;
            self.state.player_1_score = 0;
            self.state.player_2_score = 0;
            return;
        }

        let mut new_ball_x = self.state.ball_x + self.ball_dx;
        let mut new_ball_y = self.state.ball_y + self.ball_dy;

        // reflect off the top/bottom of the board
        if new_ball_y <= 0.0 || new_ball_y > BOARD_HEIGHT {
            self.ball_dy = -self.ball_dy;
            if new_ball_y <= 0.0 {
                new_ball_y = new_ball_y.abs()
            } else {
                // Y = H-(y-H)
                new_ball_y = 2.0 * BOARD_HEIGHT - new_ball_y;
            }
            fx_log_info!("bounce off top or bottom");
        }

        // reflect off the left/right of the board, if a paddle is in the way
        if new_ball_x <= 0.0 {
            //
            // X+dx*t=0 => t=-X/dx
            // Y+dy*t=NY => NY=Y-X*dy/dx
            //
            new_ball_y = self.state.ball_y - self.state.ball_x * self.ball_dy / self.ball_dx;
            // we're about to go off of the left side
            if new_ball_y > self.state.player_1_y + (PADDLE_SIZE / 2.0)
                || new_ball_y < self.state.player_1_y - (PADDLE_SIZE / 2.0)
            {
                // player 1 missed, so player 2 gets a point and we reset
                fx_log_info!(
                    "ball {} {}, new y {}, paddle {}",
                    self.state.ball_x,
                    self.state.ball_y,
                    new_ball_y,
                    self.state.player_1_y
                );
                self.state.player_2_score += 1;
                self.new_game();
                return;
            } else {
                calc_paddle_bounce(
                    &mut self.ball_dx,
                    &mut self.ball_dy,
                    new_ball_y,
                    self.state.player_1_y,
                );
                new_ball_x = new_ball_x.abs();
                if self.ball_dx < 0.0 {
                    panic!("Ball going in the wrong direction");
                }
                fx_log_info!("bounce off left");
            }
        }
        if new_ball_x > BOARD_WIDTH {
            //
            // X+dx*t=BOARD_WIDTH => t=(BOARD_WIDTH-X)/dx
            // Y+dy*t=NY => NY=Y+(BOARD_WIDTH-X)*dy/dx
            //
            new_ball_y =
                self.state.ball_y + (BOARD_WIDTH - self.state.ball_x) * self.ball_dy / self.ball_dx;
            // we're about to go off of the right side
            if new_ball_y > self.state.player_2_y + (PADDLE_SIZE / 2.0)
                || new_ball_y < self.state.player_2_y - (PADDLE_SIZE / 2.0)
            {
                // player 2 missed, so player 1 gets a point and we reset
                fx_log_info!(
                    "ball {} {}, new y {}, paddle {}",
                    self.state.ball_x,
                    self.state.ball_y,
                    new_ball_y,
                    self.state.player_2_y
                );
                self.state.player_1_score += 1;
                self.new_game();
                return;
            } else {
                calc_paddle_bounce(
                    &mut self.ball_dx,
                    &mut self.ball_dy,
                    new_ball_y,
                    self.state.player_2_y,
                );
                // X = W-(x-W)
                new_ball_x = 2.0 * BOARD_WIDTH - new_ball_x;
                if self.ball_dx > 0.0 {
                    panic!("Ball going in the wrong direction");
                }
                fx_log_info!("bounce off right");
            }
        }

        self.state.ball_x = new_ball_x;
        self.state.ball_y = new_ball_y;
    }

    fn new_game(&mut self) {
        self.player_1.as_mut().map(|player| {
            *player.state.lock() = PlayerState::Stop;
            player.proxy.new_game(false);
        });
        self.player_2.as_mut().map(|player| {
            *player.state.lock() = PlayerState::Stop;
            player.proxy.new_game(true);
        });

        self.ball_dx = 0.0;
        while self.ball_dx.abs() < 0.2 {
            let angle: f64 = rand::random::<f64>() * 2.0 * PI;
            self.ball_dx = angle.sin();
            self.ball_dy = angle.cos();
        }
        self.ball_dx *= TIME_SCALE_FACTOR;
        self.ball_dy *= TIME_SCALE_FACTOR;
        self.state.ball_x = BOARD_WIDTH / 2.0;
        self.state.ball_y = BOARD_HEIGHT / 2.0;
        self.state.game_num += 1;
        self.state.player_1_y = BOARD_HEIGHT / 2.0;
        self.state.player_2_y = BOARD_HEIGHT / 2.0;
        self.state.time = 0;
    }
}
