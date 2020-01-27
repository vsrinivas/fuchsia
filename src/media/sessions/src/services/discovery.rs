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
use crate::{proxies::player::Player, Result, CHANNEL_BUFFER_SIZE};
use anyhow::Error;
use fidl::encoding::Decodable;
use fidl_fuchsia_media_sessions2::*;
use fuchsia_syslog::fx_log_warn;
use futures::{
    self,
    channel::mpsc,
    future::BoxFuture,
    prelude::*,
    stream::{self, BoxStream, Once, Stream},
    task::{Context, Poll},
    StreamExt,
};
use mpmc;
use std::{collections::HashMap, marker::Unpin, ops::RangeFrom, pin::Pin};
use streammap::StreamMap;

struct WatcherClient {
    event_forward: BoxFuture<'static, Result<()>>,
    disconnect_signal: BoxStream<'static, ()>,
}

impl Future for WatcherClient {
    type Output = Result<()>;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        if Pin::new(&mut self.disconnect_signal).poll_next(cx).is_ready() {
            // The client has disconnected.
            return Poll::Ready(Ok(()));
        }

        Pin::new(&mut self.event_forward).poll(cx)
    }
}

/// Implements `fuchsia.media.session2.Discovery`.
pub struct Discovery {
    player_stream: mpsc::Receiver<Player>,
    catch_up_events: HashMap<u64, FilterApplicant<(u64, PlayerEvent)>>,
    watcher_ids: RangeFrom<usize>,
    /// Clients watching the stream of player events, through a collection view or
    /// through a `SessionControl` channel.
    watchers: StreamMap<usize, Once<WatcherClient>>,
    /// Connections to player serving sessions.
    player_updates: StreamMap<u64, Player>,
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
            player_updates: StreamMap::new(),
            player_update_sender: mpmc::Sender::default(),
        }
    }

    async fn add_watcher_client(
        &mut self,
        disconnect_signal: impl Stream<Item = ()> + Unpin + Send + 'static,
        watcher_sink: impl Sink<(u64, SessionsWatcherEvent), Error = Error> + Unpin + Send + 'static,
        player_events: impl Stream<Item = FilterApplicant<(u64, PlayerEvent)>> + Unpin + Send + 'static,
        filter: Filter,
    ) {
        let queue: Vec<FilterApplicant<(u64, PlayerEvent)>> =
            self.catch_up_events.values().cloned().collect();
        let event_stream =
            stream::iter(queue).chain(player_events).filter_map(watcher_filter(filter));

        let event_forward = event_stream.map(Ok).forward(watcher_sink).boxed();
        let disconnect_signal = disconnect_signal.boxed();
        let watcher = WatcherClient { event_forward, disconnect_signal };

        let id = self.watcher_ids.next().expect("Taking next element from infinite sequence");
        self.watchers.insert(id, stream::once(watcher)).await;
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
                            if let Ok(requests) = session_control_request.into_stream() {
                                let (watcher_send, recv) = mpsc::channel(CHANNEL_BUFFER_SIZE);
                                self.add_watcher_client(
                                    stream::pending(),
                                    watcher_send.sink_map_err(Error::from),
                                    self.player_update_sender.new_receiver(),
                                    Filter::new(WatchOptions {
                                        allowed_sessions: Some(vec![session_id]),
                                        ..Decodable::new_empty()
                                    })
                                ).await;

                                self.player_updates.with_elem(session_id, move |player: &mut Player| {
                                    player.serve_controls(
                                        requests,
                                        recv.filter_map(move |(id, event)| {
                                            if !(id == session_id) {
                                                fx_log_warn!(
                                                    tag: "discovery",
                                                    "Watcher did not filter sessions by id"
                                                );
                                                future::ready(None)
                                            } else {
                                                match event {
                                                    SessionsWatcherEvent::Updated(update) => {
                                                        future::ready(Some(update))
                                                    },
                                                    _ => future::ready(None)
                                                }
                                            }
                                        })
                                    );
                                }).await;
                            }
                        }
                        DiscoveryRequest::WatchSessions { watch_options, session_watcher, ..} => {
                            match session_watcher.into_proxy() {
                                Ok(proxy) => {
                                    self.add_watcher_client(
                                        proxy.take_event_stream().map(drop),
                                        FlowControlledProxySink::from(proxy),
                                        self.player_update_sender.new_receiver(),
                                        Filter::new(watch_options)
                                    ).await;
                                },
                                Err(e) => {
                                    fx_log_warn!(
                                        tag: "discovery",
                                        "Client tried to watch session with invalid watcher: {:?}",
                                        e
                                    );
                                }
                            };
                        }
                    }
                }
                // Drive dispatch of events to watcher clients.
                 _ = self.watchers.select_next_some() => {}
                // A new player has been published to `fuchsia.media.sessions2.Publisher`.
                new_player = self.player_stream.select_next_some() => {
                    self.player_updates.insert(new_player.id(), new_player).await;
                }
                // A player answered a hanging get for its status.
                player_update = self.player_updates.select_next_some() => {
                    let (id, event) = &player_update.applicant;
                    if let PlayerEvent::Removed = event {
                        self.catch_up_events.remove(id);
                        if let Some(mut player) = self.player_updates.remove(*id).await {
                            player.disconnect_proxied_clients().await;
                        }
                    } else {
                        self.catch_up_events.insert(*id, player_update.clone());
                    }
                    self.player_update_sender.send(player_update).await;
                }
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::spawn_log_error;
    use fidl::{encoding::Decodable, endpoints::create_endpoints};
    use fuchsia_async as fasync;
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
        let (player_client, player_server) = create_endpoints::<PlayerMarker>()?;
        let player = Player::new(
            player_client,
            PlayerRegistration { domain: Some(String::from("test_domain://")) },
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
