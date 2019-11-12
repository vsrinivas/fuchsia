// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{proxies::player::Player, Result};
use failure::ResultExt;
use fidl_fuchsia_media_sessions2::*;
use futures::{channel::mpsc, prelude::*};

/// Implements `fuchsia.media.session2.Publisher`.
#[derive(Clone)]
pub struct Publisher {
    player_sink: mpsc::Sender<Player>,
}

impl Publisher {
    pub fn new(player_sink: mpsc::Sender<Player>) -> Self {
        Self { player_sink }
    }

    pub async fn serve(mut self, mut request_stream: PublisherRequestStream) -> Result<()> {
        while let Some(request) = request_stream.try_next().await.context("Publisher requests")? {
            let (player, registration, responder) = match request {
                PublisherRequest::PublishPlayer { player, registration, .. } => {
                    (player, registration, None)
                }
                PublisherRequest::Publish { player, registration, responder } => {
                    (player, registration, Some(responder))
                }
            };

            let player_result = (move || -> Result<Player> {
                let player = Player::new(player, registration)?;
                if let Some(responder) = responder {
                    responder.send(player.id())?;
                }

                Ok(player)
            })();

            match player_result {
                Ok(player) => self.player_sink.send(player).await?,
                Err(e) => {
                    eprintln!("A request to publish a player was invalid: {:?}", e);
                }
            }
        }

        Ok(())
    }
}
