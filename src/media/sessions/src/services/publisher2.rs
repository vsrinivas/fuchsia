// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Result;
use failure::ResultExt;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_media_sessions2::*;
use fidl_table_validation::*;
use futures::{channel::mpsc, prelude::*};
use std::convert::TryFrom;

#[derive(Debug, Clone, ValidFidlTable)]
#[fidl_table_src(PlayerRegistration)]
pub struct ValidPlayerRegistration {
    pub domain: String,
}

#[derive(Debug, Clone)]
pub struct NewPlayer {
    pub proxy: PlayerProxy,
    pub registration: ValidPlayerRegistration,
}

impl NewPlayer {
    fn new(client_end: ClientEnd<PlayerMarker>, registration: PlayerRegistration) -> Result<Self> {
        Ok(Self {
            proxy: client_end.into_proxy()?,
            registration: ValidPlayerRegistration::try_from(registration)?,
        })
    }
}

/// Implements `fuchsia.media.session2.Publisher`.
#[derive(Clone)]
pub struct Publisher {
    player_sink: mpsc::Sender<NewPlayer>,
}

impl Publisher {
    pub fn new(player_sink: mpsc::Sender<NewPlayer>) -> Self {
        Self { player_sink }
    }

    pub async fn serve(mut self, mut request_stream: PublisherRequestStream) -> Result<()> {
        while let Some(request) = request_stream.try_next().await.context("Publisher requests")? {
            match request {
                PublisherRequest::PublishPlayer { player, registration, .. } => {
                    match NewPlayer::new(player, registration) {
                        Ok(new_player) => self.player_sink.send(new_player).await?,
                        Err(e) => {
                            eprintln!("A request to publish a player was invalid: {:?}", e);
                        }
                    }
                }
            }
        }

        Ok(())
    }
}
