// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{proxies::player::PlayerProxyEvent, Result, SessionId, CHANNEL_BUFFER_SIZE};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_media_sessions2::*;
use fuchsia_syslog::fx_log_warn;
use futures::{
    channel::mpsc,
    future,
    stream::{BoxStream, SelectAll, Stream, StreamExt},
};
use std::{
    collections::{BTreeMap, HashMap},
    ops::RangeFrom,
};

const LOG_TAG: &str = "active_session";

trait InfiniteIter {
    fn next_trusted(&mut self) -> usize;
}

impl InfiniteIter for RangeFrom<usize> {
    fn next_trusted(&mut self) -> usize {
        self.next().expect("Taking next element from an infinite sequence")
    }
}

type ClientId = usize;

pub enum ActiveSessionInputEvent {
    PlayerProxy((SessionId, PlayerProxyEvent)),
    NewClient(ActiveSessionRequestStream),
    ClientRequest((ClientId, ActiveSessionWatchActiveSessionResponder)),
}

#[derive(Default)]
struct Client {
    hanging_get: Option<ActiveSessionWatchActiveSessionResponder>,
    last_session_sent: Option<Option<SessionId>>,
}

impl Client {
    fn is_out_of_date_with(&self, current: Option<SessionId>) -> bool {
        self.last_session_sent.map(|last_session_sent| last_session_sent != current).unwrap_or(true)
    }
}

/// A stamp indicating the time at which activity occurred. A higher stamp is later in time.
type ActivityStamp = usize;

/// A bimap of SessionId<->ActivityStamp, to track which session is the most recently
/// active.
#[derive(Debug)]
struct ActiveSessionBiMap {
    stamps: RangeFrom<usize>,
    id_to_stamp: HashMap<SessionId, ActivityStamp>,
    stamp_to_id: BTreeMap<ActivityStamp, SessionId>,
}

impl Default for ActiveSessionBiMap {
    fn default() -> Self {
        Self { stamps: 0.., id_to_stamp: HashMap::default(), stamp_to_id: BTreeMap::default() }
    }
}

impl ActiveSessionBiMap {
    pub fn active_session(&self) -> Option<SessionId> {
        self.stamp_to_id.values().next_back().copied()
    }

    /// Promotes the session to most recently active. Returns true if this was
    /// not already the most recently active session.
    pub fn promote_session(&mut self, session_id: SessionId) -> bool {
        let current_active_session = self.active_session();
        let activity_stamp = self.stamps.next_trusted();
        if let Some(old_stamp) = self.id_to_stamp.remove(&session_id) {
            self.stamp_to_id.remove(&old_stamp);
        }

        self.id_to_stamp.insert(session_id, activity_stamp);
        self.stamp_to_id.insert(activity_stamp, session_id);

        current_active_session != Some(session_id)
    }

    /// Removes a session. Returns true if this was the most recently active session.
    pub fn remove_session(&mut self, session_id: SessionId) -> bool {
        let current_active_session = self.active_session();
        let stamp = self.id_to_stamp.remove(&session_id);
        stamp
            .map(|stamp| current_active_session == self.stamp_to_id.remove(&stamp))
            .unwrap_or(false)
    }
}

/// Implements the `ActiveSession` FIDL protocol.
pub struct ActiveSession {
    connect_to_session: Box<dyn Fn(SessionId) -> Result<ClientEnd<SessionControlMarker>>>,
    active_sessions: ActiveSessionBiMap,
    input_stream: SelectAll<BoxStream<'static, ActiveSessionInputEvent>>,
    client_ids: RangeFrom<ClientId>,
    clients: HashMap<ClientId, Client>,
}

impl ActiveSession {
    pub fn new(
        player_events: impl Stream<Item = (SessionId, PlayerProxyEvent)> + Unpin + Send + 'static,
        connect_to_session: impl Fn(SessionId) -> Result<ClientEnd<SessionControlMarker>> + 'static,
    ) -> Result<(Self, mpsc::Sender<ActiveSessionRequestStream>)> {
        let mut input_stream = SelectAll::new();
        input_stream.push(player_events.map(ActiveSessionInputEvent::PlayerProxy).boxed());

        let (client_sink, client_stream) = mpsc::channel(CHANNEL_BUFFER_SIZE);
        input_stream.push(client_stream.map(ActiveSessionInputEvent::NewClient).boxed());

        Ok((
            Self {
                connect_to_session: Box::new(connect_to_session),
                active_sessions: ActiveSessionBiMap::default(),
                input_stream,
                client_ids: 0..,
                clients: HashMap::new(),
            },
            client_sink,
        ))
    }

    fn active_session(&self) -> Result<Option<ClientEnd<SessionControlMarker>>> {
        self.active_sessions
            .active_session()
            .map(|active_session_id| (self.connect_to_session)(active_session_id))
            .transpose()
    }

    fn update_clients(&mut self) -> Result<()> {
        let client_ids: Vec<ClientId> = self.clients.keys().copied().collect();
        for client_id in client_ids {
            self.update_client(client_id)?;
        }

        Ok(())
    }

    fn update_client(&mut self, client_id: ClientId) -> Result<()> {
        let responder = self.clients.get_mut(&client_id).and_then(|c| c.hanging_get.take());
        let responder = match responder {
            Some(responder) => responder,
            None => return Ok(()),
        };

        let active_session_id = self.active_sessions.active_session();
        let active_session = self.active_session()?;
        if let Err(fidl_error) = responder.send(active_session) {
            fx_log_warn!(tag: LOG_TAG, "Disconnecting Active Session client: {:?}", fidl_error);
            self.clients.remove(&client_id);
        }

        self.clients
            .get_mut(&client_id)
            .iter_mut()
            .for_each(|client| client.last_session_sent = Some(active_session_id));
        Ok(())
    }

    pub async fn serve(mut self) -> Result<()> {
        use ActiveSessionInputEvent::*;

        while let Some(input) = self.input_stream.next().await {
            match input {
                PlayerProxy((session_id, event)) => match event {
                    PlayerProxyEvent::Updated(f) => {
                        let session_info_delta = f();

                        let active = session_info_delta.is_locally_active.unwrap_or(false);
                        let update = active && self.active_sessions.promote_session(session_id);

                        if update {
                            self.update_clients()?;
                        }
                    }
                    PlayerProxyEvent::Removed => {
                        let update = self.active_sessions.remove_session(session_id);

                        if update {
                            self.update_clients()?;
                        }
                    }
                },
                NewClient(client_requests) => {
                    let client_id = self.client_ids.next_trusted();
                    self.clients.insert(client_id, Client::default());
                    self.input_stream.push(
                        client_requests
                            .take_while(|r| future::ready(r.is_ok()))
                            .filter_map(|r| future::ready(r.ok()))
                            .filter_map(|r| future::ready(r.into_watch_active_session()))
                            .map(move |responder| (client_id, responder))
                            .map(ActiveSessionInputEvent::ClientRequest)
                            .boxed(),
                    )
                }
                ClientRequest((client_id, responder)) => {
                    let update = (|| {
                        let current_active_session = self.active_sessions.active_session();
                        let mut client = self.clients.entry(client_id).or_insert(Client::default());

                        if client.hanging_get.is_some() {
                            return false;
                        }
                        client.hanging_get = Some(responder);

                        client.is_out_of_date_with(current_active_session)
                    })();

                    if update {
                        self.update_client(client_id)?;
                    }
                }
            }
        }

        Ok(())
    }
}
