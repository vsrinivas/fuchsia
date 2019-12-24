// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_game_tennis::{PaddleRequest, TennisServiceMarker};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use futures::TryStreamExt;
use parking_lot::Mutex;
use std::io::{self, Read};
use std::sync::Arc;
fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let tennis_service = connect_to_service::<TennisServiceMarker>()?;
    let (client_end, paddle_controller) = create_endpoints()?;
    let (mut prs, paddle_control_handle) = paddle_controller.into_stream_and_control_handle()?;
    tennis_service.register_paddle("Human", client_end)?;
    let i_am_player_2 = Arc::new(Mutex::new(false));
    let i_am_player_2_clone = i_am_player_2.clone();
    println!("registering with game service");
    fasync::spawn(async move {
        while let Some(PaddleRequest::NewGame { is_player_2, .. }) = prs.try_next().await.unwrap() {
            // TODO: remove unwrap
            if is_player_2 {
                println!("I am player 2");
            } else {
                println!("I am player 1");
            }
            *i_am_player_2_clone.lock() = is_player_2
        }
    });
    let resp: Result<(), Error> = executor.run_singlethreaded(async move {
        while let Some(input) = io::stdin().lock().bytes().next() {
            match input {
                Ok(65) => {
                    println!("moving up");
                    paddle_control_handle.send_up()?;
                }
                Ok(66) => {
                    println!("moving down");
                    paddle_control_handle.send_down()?;
                }
                Ok(32) => {
                    println!("stopping");
                    paddle_control_handle.send_stop()?;
                }
                Ok(_) => (),
                Err(e) => println!("Error is {:?}", e),
            };
        }
        Ok(())
    });
    resp
}
