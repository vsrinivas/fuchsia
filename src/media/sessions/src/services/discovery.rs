// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod filter;
pub mod player_event;
mod watcher;

use self::{
    filter::*,
    player_event::{PlayerEvent, SessionsWatcherEvent},
    watcher::*,
};
use crate::{
    proxies::{
        observer::*,
        player::{ForwardControlRequest, Player},
    },
    Result, CHANNEL_BUFFER_SIZE,
};
use fidl::{
    encoding::Decodable,
    endpoints::{ClientEnd, ServerEnd},
};
use fidl_fuchsia_media_sessions2::*;
use fuchsia_syslog::fx_log_warn;
use futures::{
    self,
    channel::mpsc,
    future::BoxFuture,
    prelude::*,
    stream::{self, Once, Stream},
    StreamExt,
};
use mpmc;
use std::{collections::HashMap, convert::TryFrom, marker::Unpin, ops::RangeFrom};
use streammap::StreamMap;

const LOG_TAG: &str = "discovery";

type Task = Once<BoxFuture<'static, Result<()>>>;

/// Implements `fuchsia.media.session2.Discovery`.
pub struct Discovery {
    player_stream: mpsc::Receiver<Player>,
    catch_up_events: HashMap<u64, FilterApplicant<(u64, PlayerEvent)>>,
    watcher_ids: RangeFrom<usize>,
    /// Clients watching the stream of player events, through a collection view or
    /// through a `SessionControl` channel.
    watchers: StreamMap<usize, Task>,
    /// Connections to player serving sessions.
    players: StreamMap<u64, Player>,
    /// The sender through which we distribute player events to all watchers.
    player_update_sender: mpmc::Sender<FilterApplicant<(u64, PlayerEvent)>>,
}

impl Discovery {
    pub fn new(player_stream: mpsc::Receiver<Player>) -> Self {
        Self {
            player_stream,
            catch_up_events: HashMap::new(),
            watcher_ids: 0..,
            watchers: StreamMap::new(),
            players: StreamMap::new(),
            player_update_sender: mpmc::Sender::default(),
        }
    }

    fn make_session_info_stream(
        &mut self,
        filter: Filter,
    ) -> impl Stream<Item = (u64, SessionsWatcherEvent)> + Unpin + Send + 'static {
        let queue: Vec<FilterApplicant<(u64, PlayerEvent)>> =
            self.catch_up_events.values().cloned().collect();
        stream::iter(queue)
            .chain(self.player_update_sender.new_receiver())
            .filter_map(watcher_filter(filter))
    }

    /// Connects a client to a single session if that session exists.
    async fn connect_to_session(
        &mut self,
        session_id: u64,
        session_control_request: ServerEnd<SessionControlMarker>,
    ) {
        let mut requests = match session_control_request.into_stream() {
            Ok(requests) => requests,
            Err(e) => {
                fx_log_warn!(
                    tag: LOG_TAG,
                    "Client attempted to connect to session with bad channel: {:?}",
                    e
                );
                return;
            }
        };

        let session_info_stream = self
            .make_session_info_stream(Filter::new(WatchOptions {
                allowed_sessions: Some(vec![session_id]),
                ..Decodable::new_empty()
            }))
            .filter_map(move |(id, event)| {
                if id != session_id {
                    fx_log_warn!(tag: LOG_TAG, "Watcher did not filter sessions by id");
                    future::ready(None)
                } else {
                    match event {
                        SessionsWatcherEvent::Updated(update) => future::ready(Some(update)),
                        _ => future::ready(None),
                    }
                }
            });

        let (mut control_request_sink, control_request_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);
        let (mut status_request_sink, status_request_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);

        let partitioner = move || async move {
            while let Some(request) = requests.try_next().await? {
                match ForwardControlRequest::try_from(request) {
                    Ok(control_request) => control_request_sink.send(control_request).await?,
                    Err(status_request) => status_request_sink.send(status_request).await?,
                }
            }

            Result::Ok(())
        };
        let partitioner = partitioner();
        let observer = Observer::new(session_info_stream, status_request_stream);
        let task = future::join(partitioner, observer).map(drop).boxed();

        self.players
            .with_elem(session_id, move |player: &mut Player| {
                player.serve_controls(control_request_stream);
                player.add_proxy_task(task);
            })
            .await;
    }

    /// Connects a client to the set of all sessions.
    async fn watch_sessions(
        &mut self,
        watch_options: WatchOptions,
        session_watcher: ClientEnd<SessionsWatcherMarker>,
    ) {
        let proxy = match session_watcher.into_proxy() {
            Ok(proxy) => proxy,
            Err(e) => {
                fx_log_warn!(
                    tag: LOG_TAG,
                    "Client tried to watch session with invalid watcher: {:?}",
                    e
                );
                return;
            }
        };

        let session_info_stream = self.make_session_info_stream(Filter::new(watch_options));
        let sink = FlowControlledProxySink::from(proxy);
        let event_forward = session_info_stream.map(Ok).forward(sink).boxed();

        let id = self.watcher_ids.next().expect("Taking next element from infinite sequence");
        self.watchers.insert(id, stream::once(event_forward)).await;
    }

    async fn handle_player_update(&mut self, player_update: FilterApplicant<(u64, PlayerEvent)>) {
        let (id, event) = &player_update.applicant;
        if let PlayerEvent::Removed = event {
            self.catch_up_events.remove(id);
        } else {
            self.catch_up_events.insert(*id, player_update.clone());
        }
        self.player_update_sender.send(player_update).await;
    }

    pub async fn serve(
        mut self,
        mut request_stream: mpsc::Receiver<DiscoveryRequest>,
    ) -> Result<()> {
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
                            self.connect_to_session(session_id, session_control_request).await;
                        }
                        DiscoveryRequest::WatchSessions { watch_options, session_watcher, ..} => {
                            self.watch_sessions(watch_options, session_watcher).await;
                        }
                    }
                }
                // Drive dispatch of events to watcher clients.
                 _ = self.watchers.select_next_some() => {}
                // A new player has been published to `fuchsia.media.sessions2.Publisher`.
                new_player = self.player_stream.select_next_some() => {
                    self.players.insert(new_player.id(), new_player).await;
                }
                // A player answered a hanging get for its status.
                player_update = self.players.select_next_some() => {
                    self.handle_player_update(player_update).await;
                }
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{id::Id, spawn_log_error};
    use fidl::{encoding::Decodable, endpoints::create_endpoints};
    use fuchsia_async as fasync;
    use fuchsia_inspect::Inspector;
    use test_util::assert_matches;

    #[fasync::run_singlethreaded]
    #[test]
    async fn watchers_caught_up_to_existing_players() -> Result<()> {
        let (mut player_sink, player_stream) = mpsc::channel(100);
        let (mut request_sink, request_stream) = mpsc::channel(100);
        let dummy_control_handle =
            create_endpoints::<DiscoveryMarker>()?.1.into_stream_and_control_handle()?.1;

        let under_test = Discovery::new(player_stream);
        spawn_log_error(under_test.serve(request_stream));

        // Create one watcher ahead of any players, for synchronization.
        let (watcher1_client, watcher1_server) = create_endpoints::<SessionsWatcherMarker>()?;
        let mut watcher1 = watcher1_server.into_stream()?;
        request_sink
            .send(DiscoveryRequest::WatchSessions {
                watch_options: Decodable::new_empty(),
                session_watcher: watcher1_client,
                control_handle: dummy_control_handle.clone(),
            })
            .await?;

        // Add a player to the set, and vend an update from it.
        let inspector = Inspector::new();
        let (player_client, player_server) = create_endpoints::<PlayerMarker>()?;
        let player = Player::new(
            Id::new()?,
            player_client,
            PlayerRegistration {
                domain: Some(String::from("test_domain://")),
                ..Decodable::new_empty()
            },
            inspector.root().create_string("test_player", ""),
        )?;
        player_sink.send(player).await?;
        let mut player_requests = player_server.into_stream()?;
        let info_change_responder = player_requests
            .try_next()
            .await?
            .expect("Receiving a request")
            .into_watch_info_change()
            .expect("Receiving info change responder");
        info_change_responder.send(Decodable::new_empty())?;

        // Synchronize with the first watcher. After receiving this, we know that the service knows
        // about the player.
        assert_matches!(watcher1.try_next().await?, Some(_));

        // A new watcher connecting after the registration of the player should be caught up
        // with the existence of the player.
        let (watcher2_client, watcher2_server) = create_endpoints::<SessionsWatcherMarker>()?;
        request_sink
            .send(DiscoveryRequest::WatchSessions {
                watch_options: Decodable::new_empty(),
                session_watcher: watcher2_client,
                control_handle: dummy_control_handle.clone(),
            })
            .await?;
        let mut watcher2 = watcher2_server.into_stream()?;
        assert_matches!(watcher2.try_next().await?.and_then(|r| r.into_session_updated()), Some(_));

        Ok(())
    }
}
