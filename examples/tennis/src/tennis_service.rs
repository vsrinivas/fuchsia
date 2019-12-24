// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::game::{Game, PlayerState};
use anyhow::Context;
use fidl_fuchsia_game_tennis as fidl_tennis;
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_zircon::DurationNum;
use futures::prelude::*;
use parking_lot::Mutex;
use std::sync::Arc;

#[derive(Clone)]
pub struct TennisService(Arc<Mutex<Game>>);

impl TennisService {
    pub fn new() -> TennisService {
        TennisService(Arc::new(Mutex::new(Game::new())))
    }

    pub fn bind(&self, mut stream: fidl_tennis::TennisServiceRequestStream) {
        let self_clone = self.clone();
        fuchsia_async::spawn(
            async move {
                while let Some(msg) = stream
                    .try_next()
                    .await
                    .context("error reading value from tennis service request stream")?
                {
                    match msg {
                        fidl_tennis::TennisServiceRequest::GetState { responder, .. } => {
                            let TennisService(game_arc) = &self_clone;
                            responder
                                .send(&mut game_arc.lock().state())
                                .context("error sending GetState response")?;
                        }
                        fidl_tennis::TennisServiceRequest::RegisterPaddle {
                            player_name,
                            paddle,
                            ..
                        } => {
                            fx_log_info!("new paddle registered: {}", player_name);
                            self_clone.register_paddle(player_name, paddle);
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        );
    }

    pub fn register_paddle(
        &self,
        player_name: String,
        paddle: fidl::endpoints::ClientEnd<fidl_fuchsia_game_tennis::PaddleMarker>,
    ) {
        let TennisService(game_arc) = self.clone();
        let mut game = game_arc.lock();
        let paddle_proxy = paddle.into_proxy().unwrap();
        let mut stream = paddle_proxy.take_event_stream();
        let player_state = game.register_new_paddle(player_name.clone(), paddle_proxy);
        fasync::spawn(
            async move {
                while let Some(event) = stream
                    .try_next()
                    .await
                    .context("error reading value from paddle event stream")?
                {
                    let state = match event {
                        fidl_tennis::PaddleEvent::Up { .. } => PlayerState::Up,
                        fidl_tennis::PaddleEvent::Down { .. } => PlayerState::Down,
                        fidl_tennis::PaddleEvent::Stop { .. } => PlayerState::Stop,
                    };
                    *player_state.lock() = state;
                }
                *player_state.lock() = PlayerState::Disconnected;
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        );
        if game.players_ready() {
            fx_log_info!("game is beginning");
            let game_arc = game_arc.clone();
            fasync::spawn(async move {
                loop {
                    game_arc.lock().step();
                    let time_step: i64 = (100.0 * game_arc.lock().time_scale_factor()) as i64;
                    fuchsia_async::Timer::new(time_step.millis().after_now()).await;
                    if !game_arc.lock().players_ready() {
                        fx_log_info!("someone disconnected!");
                        return;
                    }
                }
            });
        }
    }
}
