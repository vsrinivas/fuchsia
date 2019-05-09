// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    active_session_queue::ActiveSessionQueue, clone_session_id_handle, mpmc,
    session_list::SessionList, session_proxy::*, subscriber::Subscriber, Result,
};
use fidl::endpoints::RequestStream;
use fidl_fuchsia_media_sessions::{
    ActiveSession, RegistryControlHandle, RegistryRequest, RegistryRequestStream, SessionDelta,
    SessionEntry, SessionsChange,
};
use fuchsia_zircon::AsHandleRef;
use futures::{lock::Mutex, StreamExt};
use std::{ops::Deref, sync::Arc};

/// `Registry` implements `fuchsia.media.session.Registry`.
#[derive(Clone)]
pub struct Registry {
    session_list: Arc<Mutex<SessionList>>,
    active_session_queue: Arc<Mutex<ActiveSessionQueue>>,
    collection_event_stream: mpmc::Receiver<(SessionRegistration, SessionCollectionEvent)>,
    active_session_stream: mpmc::Receiver<Option<SessionRegistration>>,
}

impl Registry {
    pub fn new(
        session_list: Arc<Mutex<SessionList>>,
        active_session_queue: Arc<Mutex<ActiveSessionQueue>>,
        collection_event_stream: mpmc::Receiver<(SessionRegistration, SessionCollectionEvent)>,
        active_session_stream: mpmc::Receiver<Option<SessionRegistration>>,
    ) -> Registry {
        Registry {
            session_list,
            active_session_queue,
            collection_event_stream,
            active_session_stream,
        }
    }

    pub async fn serve(mut self, mut request_stream: RegistryRequestStream) -> Result<()> {
        let control_handle = request_stream.control_handle();
        let (mut active_session_subscriber, mut sessions_change_subscriber) = match (
            await!(self.initialize_active_session_subscriber(control_handle.clone()))?,
            await!(self.initialize_sessions_change_subscriber(control_handle.clone()))?,
        ) {
            (Some(as_sub), Some(sc_sub)) => (as_sub, sc_sub),
            _ => {
                // Client has disconnected.
                return Ok(());
            }
        };

        loop {
            futures::select! {
                request = request_stream.select_next_some() => {
                    match request? {
                        RegistryRequest::ConnectToSessionById {
                            session_id,
                            session_request,
                            ..
                        } => {
                            let koid = session_id.as_handle_ref().get_koid()?;
                            if let Some(session) = await!(self.session_list.lock()).get(koid) {
                                await!(session.connect(session_request))?;
                            }
                        }
                        RegistryRequest::NotifyActiveSessionChangeHandled { .. } => {
                            active_session_subscriber.ack();
                        }
                        RegistryRequest::NotifySessionsChangeHandled { .. } => {
                            sessions_change_subscriber.ack();
                        }
                    };
                },
                active = self.active_session_stream.select_next_some() => {
                    let should_keep_client = active_session_subscriber.should_wait_to_send_more()
                                             || active_session_subscriber.send(ActiveSession {
                                                    session_id: active
                                                        .map(|registration| {
                                                            clone_session_id_handle(
                                                                registration.id.deref()
                                                            )
                                                        })
                                                        .transpose()?
                                                });
                    if !should_keep_client {
                        return Ok(());
                    }
                },
                (registration, event) = self.collection_event_stream.select_next_some() => {
                    let entry = SessionEntry {
                        session_id: Some(clone_session_id_handle(registration.id.deref())?),
                        local: Some(registration.is_local)
                    };

                    let behind_on_acks = sessions_change_subscriber.should_wait_to_send_more();
                    let should_keep_client = behind_on_acks || match event {
                        SessionCollectionEvent::Added => {
                            sessions_change_subscriber.send(SessionsChange {
                                session: entry,
                                delta: SessionDelta::Added
                            })
                        },
                        SessionCollectionEvent::Removed => {
                            sessions_change_subscriber.send(SessionsChange{
                                session: entry,
                                delta: SessionDelta::Removed
                            })
                        }
                    };
                    if !should_keep_client {
                        return Ok(());
                    }
                }
            }
        }
    }

    async fn initialize_active_session_subscriber(
        &self,
        control_handle: RegistryControlHandle,
    ) -> Result<Option<Subscriber>> {
        let mut active_session_subscriber = Subscriber::new(control_handle.clone());
        let should_keep = active_session_subscriber.send(ActiveSession {
            session_id: await!(self.active_session_queue.lock())
                .active_session()
                .map(|registration| clone_session_id_handle(registration.id.deref()))
                .transpose()?,
        });

        if should_keep {
            Ok(Some(active_session_subscriber))
        } else {
            Ok(None)
        }
    }

    async fn initialize_sessions_change_subscriber(
        &self,
        control_handle: RegistryControlHandle,
    ) -> Result<Option<Subscriber>> {
        let sessions_change_subscriber = Subscriber::new(control_handle);
        let should_keep: bool = await!(self.session_list.lock())
            .list()
            .map(|registration| -> Result<SessionsChange> {
                Ok(SessionsChange {
                    session: SessionEntry {
                        session_id: Some(clone_session_id_handle(registration.id.deref())?),
                        local: Some(registration.is_local),
                    },
                    delta: SessionDelta::Added,
                })
            })
            .map(|r| r.map(|entry| sessions_change_subscriber.send_no_ack_count(entry)))
            .fold(Ok(true), |acc, r| {
                acc.and_then(|keep_so_far| r.map(|keep| keep && keep_so_far))
            })?;
        if should_keep {
            Ok(Some(sessions_change_subscriber))
        } else {
            Ok(None)
        }
    }
}
