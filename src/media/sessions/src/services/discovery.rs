// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod player_event;
mod watcher;

use self::{player_event::PlayerEvent, watcher::*};
use crate::{proxies::player::Player, spawn_log_error, Ref, Result};
use fidl_fuchsia_media_sessions2::*;
use fuchsia_syslog::fx_log_info;
use futures::{self, channel::mpsc, prelude::*, stream::FuturesUnordered};
use mpmc;
use rand::prelude::random;
use std::collections::hash_map::*;

/// Implements `fuchsia.media.session2.Discovery`.
pub struct Discovery {
    player_stream: mpsc::Receiver<Player>,
    players: Ref<HashMap<u64, Player>>,
}

impl Discovery {
    pub fn new(player_stream: mpsc::Receiver<Player>) -> Self {
        Self { player_stream, players: Ref::default() }
    }

    pub async fn serve(
        mut self,
        mut request_stream: mpsc::Receiver<DiscoveryRequest>,
    ) -> Result<()> {
        let mut player_updates = FuturesUnordered::new();
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
                                self.players.lock().await.get_mut(&session_id).map(|player| {
                                    player.serve_controls(requests);
                                });
                            }
                        }
                        DiscoveryRequest::WatchSessions { watch_options, session_watcher, ..} => {
                            spawn_log_error(Watcher::new(
                                watch_options,
                                self.players.lock().await
                                    .iter()
                                    .map(|(id, player)| {
                                        (
                                            *id,
                                            PlayerEvent::Updated {
                                                delta: player.state().clone(),
                                                registration: Some(player.registration().clone()),
                                                active: if player.is_active() {
                                                    Some(true)
                                                } else {
                                                    None
                                                },
                                            },
                                        )
                                    })
                                    .collect(),
                                sender.new_receiver()
                            ).await.serve(session_watcher));
                        }
                    }
                }
                // A new player has been published to `fuchsia.media.sessions2.Publisher`.
                new_player = self.player_stream.select_next_some() => {
                    // TODO(turnage): Care about collisions.
                    let id = random();
                    player_updates.push(new_player.poll(id));
                    sender.send((id, PlayerEvent::Updated{
                        delta: new_player.state().clone(),
                        registration: Some(new_player.registration().clone()),
                        active: None,
                    })).await;
                    self.players.lock().await.insert(id, new_player);
                }
                // A player answered a hanging get for its status.
                player_update = player_updates.select_next_some() => {
                    let (id, delta) = player_update;
                    match delta {
                        Ok(delta) => {
                            if let Some(player) = self.players.lock().await.get_mut(&id) {
                                player.update(delta.clone());
                                let event = PlayerEvent::Updated {
                                    active: delta.is_active(),
                                    delta,
                                    registration: None,
                                };
                                sender.send((id, event)).await;
                                player_updates.push(player.poll(id));
                            }
                        }
                        Err(e) => {
                            fx_log_info!(
                                tag: "discovery",
                                "Disconnecting  {:#?}", e);
                            if let Some(mut player)  = self.players.lock().await.remove(&id) {
                                player.disconnect_proxied_clients().await;
                            }
                            sender.send((id, PlayerEvent::Removed)).await;
                        }
                    }
                }
            }
        }
    }
}
