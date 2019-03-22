// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    clone_session_entry, clone_session_id_handle, log_error::log_error_discard_result,
    session::Session, subscriber::Subscriber, Result, CHANNEL_BUFFER_SIZE,
};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_mediasession::{
    ActiveSession, SessionDelta, SessionEntry, SessionMarker, SessionsChange,
};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc::{channel, Receiver, Sender},
    SinkExt, StreamExt,
};
use std::collections::{HashMap, VecDeque};

fn clone_sessions_change(change: &SessionsChange) -> Result<SessionsChange> {
    Ok(SessionsChange {
        session: clone_session_entry(&change.session)?,
        delta: change.delta.clone(),
    })
}

/// Tracks which session most recently reported an active status.
struct ActiveSessionQueue {
    active_sessions: VecDeque<zx::Koid>,
}

impl ActiveSessionQueue {
    pub fn new() -> Self {
        Self { active_sessions: VecDeque::new() }
    }

    /// Returns session which most recently reported an active status if it
    /// exists.
    pub fn active_session(&self) -> Option<zx::Koid> {
        self.active_sessions.front().cloned()
    }

    /// Promotes a session to the front of the queue and returns whether
    /// the front of the queue was changed.
    pub fn promote_session(&mut self, session_id: zx::Koid) -> bool {
        if self.active_session() == Some(session_id) {
            return false;
        }

        self.remove_session(session_id);
        self.active_sessions.push_front(session_id);
        return true;
    }

    /// Removes a session from the queue and returns whether the front of the
    /// queue was changed.
    pub fn remove_session(&mut self, session_id: zx::Koid) -> bool {
        if self.active_session() == Some(session_id) {
            self.active_sessions.pop_front();
            true
        } else {
            if let Some(i) = self.active_sessions.iter().position(|&id| id == session_id) {
                self.active_sessions.remove(i);
            }
            false
        }
    }
}

pub enum ServiceEvent {
    NewSession { session: Session, session_id_handle: zx::Event, is_local: bool },
    SessionClosed(zx::Koid),
    SessionActivity(zx::Koid),
    NewSessionRequest { session_id: zx::Koid, request: ServerEnd<SessionMarker> },
    NewActiveSessionSubscriber(Subscriber),
    NewSessionsChangeSubscriber(Subscriber),
    ActiveSessionSubscriberAck(zx::Koid),
    SessionsChangeSubscriberAck(zx::Koid),
}

pub struct SessionRegistration {
    session_id_handle: zx::Event,
    new_client_sink: Sender<ServerEnd<SessionMarker>>,
    is_local: bool,
}

/// The Media Session service.
pub struct Service {
    published_sessions: HashMap<zx::Koid, SessionRegistration>,
    active_session_subscribers: HashMap<zx::Koid, Subscriber>,
    sessions_change_subscribers: HashMap<zx::Koid, Subscriber>,
    /// The most recent sessions to broadcast activity.
    active_session_queue: ActiveSessionQueue,
}

impl Service {
    pub fn new() -> Self {
        Self {
            published_sessions: HashMap::new(),
            active_session_subscribers: HashMap::new(),
            sessions_change_subscribers: HashMap::new(),
            active_session_queue: ActiveSessionQueue::new(),
        }
    }

    pub async fn serve(mut self, mut fidl_stream: Receiver<ServiceEvent>) -> Result<()> {
        while let Some(service_event) = await!(fidl_stream.next()) {
            match service_event {
                ServiceEvent::NewSession { session, session_id_handle, is_local } => {
                    let session_id = session.id();
                    let (new_client_sink, new_client_stream) = channel(CHANNEL_BUFFER_SIZE);
                    self.published_sessions.insert(
                        session_id,
                        SessionRegistration {
                            new_client_sink,
                            session_id_handle: clone_session_id_handle(&session_id_handle)?,
                            is_local,
                        },
                    );
                    fasync::spawn(session.serve(new_client_stream));
                    self.broadcast_sessions_change(SessionsChange {
                        session: SessionEntry {
                            session_id: Some(session_id_handle),
                            local: Some(is_local),
                        },
                        delta: SessionDelta::Added,
                    })?;
                }
                ServiceEvent::SessionClosed(session_id) => {
                    let local = self.session_is_local(session_id);
                    if local {
                        self.handle_local_session_closed(session_id)?;
                    }
                    let registration = self
                        .published_sessions
                        .remove(&session_id)
                        .expect("A registered session should be in this map");
                    self.broadcast_sessions_change(SessionsChange {
                        session: SessionEntry {
                            session_id: Some(registration.session_id_handle),
                            local: Some(local),
                        },
                        delta: SessionDelta::Removed,
                    })?;
                }
                ServiceEvent::NewSessionRequest { session_id, request } => {
                    if let Some(registration) = self.published_sessions.get_mut(&session_id) {
                        log_error_discard_result(await!(registration
                            .new_client_sink
                            .send(request)));
                    }
                }
                ServiceEvent::NewActiveSessionSubscriber(mut subscriber) => {
                    if subscriber.send(self.active_session()?) {
                        let koid = subscriber.koid()?;
                        self.active_session_subscribers.insert(koid, subscriber);
                    }
                }
                ServiceEvent::NewSessionsChangeSubscriber(subscriber) => {
                    if self.session_list()?.into_iter().all(|session| {
                        subscriber.send_no_ack_count(SessionsChange {
                            session,
                            delta: SessionDelta::Added,
                        })
                    }) {
                        let koid = subscriber.koid()?;
                        self.sessions_change_subscribers.insert(koid, subscriber);
                    }
                }
                ServiceEvent::ActiveSessionSubscriberAck(koid) => {
                    if let Some(ref mut subscriber) = self.active_session_subscribers.get_mut(&koid)
                    {
                        subscriber.ack();
                    }
                }
                ServiceEvent::SessionsChangeSubscriberAck(koid) => {
                    if let Some(ref mut subscriber) =
                        self.sessions_change_subscribers.get_mut(&koid)
                    {
                        subscriber.ack();
                    }
                }
                ServiceEvent::SessionActivity(session_id) => {
                    if self.session_is_local(session_id) {
                        self.handle_local_session_activity(session_id)?;
                    }
                }
            }
        }
        Ok(())
    }

    /// Returns true iff the session is alive and marked as local.
    fn session_is_local(&self, session_id: zx::Koid) -> bool {
        self.published_sessions
            .get(&session_id)
            .map(|registration| registration.is_local)
            .unwrap_or(false)
    }

    fn active_session(&self) -> Result<ActiveSession> {
        Ok(ActiveSession {
            session_id: self
                .active_session_queue
                .active_session()
                .and_then(|koid| self.published_sessions.get(&koid))
                .map(|registration| clone_session_id_handle(&registration.session_id_handle))
                .transpose()?,
        })
    }

    fn session_list(&self) -> Result<Vec<SessionEntry>> {
        self.published_sessions
            .values()
            .map(|registration| {
                Ok(SessionEntry {
                    session_id: Some(clone_session_id_handle(&registration.session_id_handle)?),
                    local: Some(registration.is_local),
                })
            })
            .collect()
    }

    fn handle_local_session_closed(&mut self, session_id: zx::Koid) -> Result<()> {
        let active_session_changed = self.active_session_queue.remove_session(session_id);
        if active_session_changed {
            self.broadcast_active_session()?;
        }

        Ok(())
    }

    fn handle_local_session_activity(&mut self, session_id: zx::Koid) -> Result<()> {
        let active_session_changed = self.active_session_queue.promote_session(session_id);
        if active_session_changed {
            self.broadcast_active_session()?;
        }

        Ok(())
    }

    /// Broadcasts the active session to all subscribers and drops those which are
    /// no longer connected.
    fn broadcast_active_session(&mut self) -> Result<()> {
        let mut updates = self
            .active_session_subscribers
            .iter()
            .map(|_| self.active_session())
            .collect::<Result<Vec<ActiveSession>>>()?;
        Ok(broadcast(&mut self.active_session_subscribers, move |s| s.send(updates.swap_remove(0))))
    }

    /// Broadcasts a change to the set of sessions.
    fn broadcast_sessions_change(&mut self, change: SessionsChange) -> Result<()> {
        let mut updates = self
            .sessions_change_subscribers
            .iter()
            .map(|_| clone_sessions_change(&change))
            .collect::<Result<Vec<SessionsChange>>>()?;
        Ok(broadcast(&mut self.sessions_change_subscribers, move |s| {
            s.send(updates.swap_remove(0))
        }))
    }
}

fn broadcast(
    subscribers: &mut HashMap<zx::Koid, Subscriber>,
    mut sender: impl FnMut(&mut Subscriber) -> bool,
) {
    subscribers.retain(|_, mut subscriber| {
        subscriber.should_wait_to_send_more() || sender(&mut subscriber)
    });
}
