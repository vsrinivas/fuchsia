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
use failure::Error;
use fidl::encoding::Decodable;
use fidl_fuchsia_media_sessions2::*;
use fuchsia_syslog::fx_log_warn;
use futures::{
    self,
    channel::mpsc,
    prelude::*,
    stream::{self, Stream},
    task::{Context, Poll},
    StreamExt,
};
use mpmc;
use std::{collections::HashMap, marker::Unpin, pin::Pin};
use streammap::StreamMap;

struct WatcherClient<F, S> {
    id: usize,
    event_forward: F,
    disconnect_signal: S,
}

impl<F, S> Future for WatcherClient<F, S>
where
    F: Future<Output = Result<()>> + Unpin,
    S: Stream<Item = ()> + Unpin,
{
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
    next_watcher_id: usize,
}

impl Discovery {
    pub fn new(player_stream: mpsc::Receiver<Player>) -> Self {
        Self { player_stream, catch_up_events: HashMap::new(), next_watcher_id: 0 }
    }

    fn new_watcher_client<S: Stream<Item = ()> + Unpin>(
        &mut self,
        disconnect_signal: S,
        watcher_sink: impl Sink<(u64, SessionsWatcherEvent), Error = Error> + Unpin,
        player_events: impl Stream<Item = FilterApplicant<(u64, PlayerEvent)>> + Unpin,
        filter: Filter,
    ) -> WatcherClient<impl Future<Output = Result<()>> + Unpin, S> {
        let id = self.next_watcher_id;
        self.next_watcher_id += 1;

        let queue: Vec<FilterApplicant<(u64, PlayerEvent)>> =
            self.catch_up_events.values().cloned().collect();
        let event_stream =
            stream::iter(queue).chain(player_events).filter_map(watcher_filter(filter));

        let event_forward = event_stream.map(Ok).forward(watcher_sink);

        WatcherClient { id, event_forward, disconnect_signal }
    }

    pub async fn serve(
        mut self,
        mut request_stream: mpsc::Receiver<DiscoveryRequest>,
    ) -> Result<()> {
        let mut collection_watchers = StreamMap::new();
        let mut session_watchers = StreamMap::new();

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
                                let (watcher_send, recv) = mpsc::channel(CHANNEL_BUFFER_SIZE);
                                let watcher_client = self.new_watcher_client(
                                    stream::pending(),
                                    watcher_send.sink_map_err(Error::from),
                                    sender.new_receiver(),
                                    Filter::new(WatchOptions {
                                        allowed_sessions: Some(vec![session_id]),
                                        ..Decodable::new_empty()
                                    })
                                );

                                session_watchers.insert(
                                    watcher_client.id,
                                    stream::once(watcher_client).fuse()
                                ).await;
                                player_updates.with_elem(session_id, move |player: &mut Player| {
                                    player.serve_controls(
                                        requests,
                                        recv.filter_map(move |(id, event)| {
                                            if !(id == session_id) {
                                                fx_log_warn!(
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
                                    let watcher_client = self.new_watcher_client(
                                        proxy.take_event_stream().map(drop),
                                        FlowControlledProxySink::from(proxy),
                                        sender.new_receiver(),
                                        Filter::new(watch_options)
                                    );
                                    collection_watchers.insert(
                                        watcher_client.id,
                                        stream::once(watcher_client).fuse()
                                    ).await;
                                },
                                Err(e) => {
                                    fx_log_warn!(
                                        tag: "mediasession",
                                        "Client tried to watch session with invalid watcher: {:?}",
                                        e
                                    );
                                }
                            };
                        }
                    }
                }
                // Drive dispatch of events to watcher clients.
                _ = collection_watchers.select_next_some() => {}
                _ = session_watchers.select_next_some() => {}
                // A new player has been published to `fuchsia.media.sessions2.Publisher`.
                new_player = self.player_stream.select_next_some() => {
                    player_updates.insert(new_player.id(), new_player).await;
                }
                // A player answered a hanging get for its status.
                player_update = player_updates.select_next_some() => {
                    let (id, event) = &player_update.applicant;
                    if let PlayerEvent::Removed = event {
                        self.catch_up_events.remove(id);
                        if let Some(mut player) = player_updates.remove(*id).await {
                            player.disconnect_proxied_clients().await;
                        }
                    } else {
                        self.catch_up_events.insert(*id, player_update.clone());
                    }
                    sender.send(player_update).await;
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
