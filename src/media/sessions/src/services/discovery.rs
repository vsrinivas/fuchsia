// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod player_event;
mod watcher;

use self::{player_event::PlayerEvent, watcher::*};
use crate::{proxies::player::Player, spawn_log_error, Result};
use fidl_fuchsia_media_sessions2::*;
use futures::{self, channel::mpsc, prelude::*};
use mpmc;
use streammap::StreamMap;

/// Implements `fuchsia.media.session2.Discovery`.
pub struct Discovery {
    player_stream: mpsc::Receiver<Player>,
}

impl Discovery {
    pub fn new(player_stream: mpsc::Receiver<Player>) -> Self {
        Self { player_stream }
    }

    pub async fn serve(
        mut self,
        mut request_stream: mpsc::Receiver<DiscoveryRequest>,
    ) -> Result<()> {
        let mut player_updates = StreamMap::new();
        let sender = mpmc::Sender::default();

        // Loop forever. All input channels live the life of the service, so we will always have a
        // stream to poll.
        loop {
            futures::select! {
                // A request has come in from any of the potentially many clients connected to the
                // discovery service.
                request = request_stream.select_next_some() => {
                    match request {
                        DiscoveryRequest::ConnectToSession {
                            session_id, session_control_request, ..
                        } => {
                            if let Ok(requests) = session_control_request.into_stream() {
                                player_updates.with_elem(session_id, |player: &mut Player| {
                                    player.serve_controls(requests);
                                }).await;
                            }
                        }
                        DiscoveryRequest::WatchSessions { watch_options, session_watcher, ..} => {
                            spawn_log_error(Watcher::new(
                                watch_options,
                                sender.new_receiver()
                            ).serve(session_watcher));
                        }
                    }
                }
                // A new player has been published to `fuchsia.media.sessions2.Publisher`.
                new_player = self.player_stream.select_next_some() => {
                    player_updates.insert(new_player.id(), new_player).await;
                }
                // A player answered a hanging get for its status.
                player_update = player_updates.select_next_some() => {
                    let (id, event) = player_update;
                    if let PlayerEvent::Removed = event {
                        if let Some(mut player) = player_updates.remove(id).await {
                            player.disconnect_proxied_clients().await;
                        }
                    }
                    sender.send((id, event)).await;
                }
            }
        }
    }
}
