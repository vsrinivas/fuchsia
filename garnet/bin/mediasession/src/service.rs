// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log_error::log_error_discard_result;
use crate::session::Session;
use crate::session_id_rights;
use crate::Result;
use crate::CHANNEL_BUFFER_SIZE;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_mediasession::{ActiveSession, RegistryControlHandle, SessionMarker};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc::{channel, Receiver, Sender},
    SinkExt, StreamExt,
};
use std::collections::{HashMap, VecDeque};
use zx::AsHandleRef;

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
    NewSession((Session, zx::Event)),
    SessionClosed(zx::Koid),
    SessionActivity(zx::Koid),
    NewSessionRequest { session_id: zx::Koid, request: ServerEnd<SessionMarker> },
    NewActiveSessionChangeListener(RegistryControlHandle),
}

/// The Media Session service.
pub struct Service {
    published_sessions: HashMap<zx::Koid, (Sender<ServerEnd<SessionMarker>>, zx::Event)>,
    active_session_listeners: Vec<RegistryControlHandle>,
    /// The most recent sessions to broadcast activity.
    active_session_queue: ActiveSessionQueue,
}

impl Service {
    pub fn new() -> Self {
        Self {
            published_sessions: HashMap::new(),
            active_session_listeners: Vec::new(),
            active_session_queue: ActiveSessionQueue::new(),
        }
    }

    pub async fn serve(mut self, mut fidl_stream: Receiver<ServiceEvent>) -> Result<()> {
        while let Some(service_event) = await!(fidl_stream.next()) {
            match service_event {
                ServiceEvent::NewSession((session, session_id_handle)) => {
                    let session_id = session.id();
                    let (request_sink, request_stream) = channel(CHANNEL_BUFFER_SIZE);
                    self.published_sessions.insert(session_id, (request_sink, session_id_handle));
                    fasync::spawn(session.serve(request_stream));
                }
                ServiceEvent::SessionClosed(session_id) => {
                    self.published_sessions.remove(&session_id);
                    let active_session_changed =
                        self.active_session_queue.remove_session(session_id);
                    if active_session_changed {
                        self.broadcast_active_session()?;
                    }
                }
                ServiceEvent::NewSessionRequest { session_id, request } => {
                    if let Some((request_sink, _)) = self.published_sessions.get_mut(&session_id) {
                        log_error_discard_result(await!(request_sink.send(request)));
                    }
                }
                ServiceEvent::NewActiveSessionChangeListener(listener) => {
                    if self.send_active_session(&listener).is_ok() {
                        self.active_session_listeners.push(listener);
                    }
                }
                ServiceEvent::SessionActivity(session_id) => {
                    let active_session_changed =
                        self.active_session_queue.promote_session(session_id);
                    if active_session_changed {
                        self.broadcast_active_session()?;
                    }
                }
            }
        }
        Ok(())
    }

    /// Broadcasts the active session to all listeners and drops those which are
    /// no longer connected.
    fn broadcast_active_session(&mut self) -> Result<()> {
        let mut active_session_listeners = Vec::new();
        std::mem::swap(&mut self.active_session_listeners, &mut active_session_listeners);
        active_session_listeners.retain(|listener| self.send_active_session(listener).is_ok());
        std::mem::swap(&mut self.active_session_listeners, &mut active_session_listeners);
        Ok(())
    }

    fn send_active_session(&self, recipient: &RegistryControlHandle) -> Result<()> {
        recipient.send_on_active_session(self.active_session_update()?).map_err(Into::into)
    }

    fn active_session_update(&self) -> Result<ActiveSession> {
        Ok(ActiveSession {
            session_id: self
                .active_session_queue
                .active_session()
                .and_then(|koid| self.published_sessions.get(&koid))
                .map(|(_, session_id)| -> Result<zx::Event> {
                    Ok(session_id.as_handle_ref().duplicate(session_id_rights())?.into())
                })
                .transpose()?,
        })
    }
}
