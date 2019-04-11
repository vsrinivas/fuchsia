// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    active_session_queue::ActiveSessionQueue,
    clone_session_id_handle, mpmc,
    session_list::SessionList,
    session_proxy::{Session, SessionCollectionEvent, SessionRegistration},
    Result,
};
use failure::ResultExt;
use fidl::endpoints::{ClientEnd, RequestStream, DiscoverableService};
use fidl_fuchsia_mediasession::{
    PublisherMarker, PublisherRequest, PublisherRequestStream, SessionMarker,
};
use fuchsia_app::server::ServiceFactory;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{lock::Mutex, TryFutureExt, TryStreamExt};
use std::{ops::DerefMut, sync::Arc};
use zx::AsHandleRef;

/// `Publisher` implements fuchsia.media.session.Publisher.
#[derive(Clone)]
pub struct Publisher {
    session_list: Arc<Mutex<SessionList>>,
    active_session_queue: Arc<Mutex<ActiveSessionQueue>>,
    collection_event_sink: mpmc::Sender<(SessionRegistration, SessionCollectionEvent)>,
    active_session_sink: mpmc::Sender<Option<SessionRegistration>>,
}

impl Publisher {
    pub fn factory(
        session_list: Arc<Mutex<SessionList>>,
        active_session_queue: Arc<Mutex<ActiveSessionQueue>>,
        collection_event_sink: mpmc::Sender<(SessionRegistration, SessionCollectionEvent)>,
        active_session_sink: mpmc::Sender<Option<SessionRegistration>>,
    ) -> impl ServiceFactory {
        let publisher = Publisher {
            session_list,
            active_session_queue,
            collection_event_sink,
            active_session_sink,
        };
        (PublisherMarker::NAME, move |channel| {
            fasync::spawn(publisher.clone().serve(channel).unwrap_or_else(|e| eprintln!("{}", e)))
        })
    }

    async fn serve(mut self, channel: fasync::Channel) -> Result<()> {
        let mut request_stream = PublisherRequestStream::from_channel(channel);
        while let Some(request) =
            await!(request_stream.try_next()).context("Publisher server request stream")?
        {
            match request {
                PublisherRequest::Publish { responder, session } => {
                    responder
                        .send(await!(self.publish(session, true))?)
                        .context("Giving id to client")?;
                }
                PublisherRequest::PublishRemote { responder, session } => {
                    responder
                        .send(await!(self.publish(session, false))?)
                        .context("Giving id to client")?;
                }
            };
        }
        Ok(())
    }

    async fn publish(
        &mut self,
        session: ClientEnd<SessionMarker>,
        is_local: bool,
    ) -> Result<zx::Event> {
        let session_id_handle = zx::Event::create()?;
        let koid = session_id_handle.as_handle_ref().get_koid()?;
        let handle_for_client = clone_session_id_handle(&session_id_handle)?;
        let registration = SessionRegistration { id: Arc::new(session_id_handle), koid, is_local };
        await!(self.session_list.lock()).deref_mut().push(
            registration.clone(),
            await!(Session::serve(
                session,
                registration.clone(),
                self.active_session_queue.clone(),
                self.session_list.clone(),
                self.collection_event_sink.clone(),
                self.active_session_sink.clone(),
            ))?,
        );
        await!(self
            .collection_event_sink
            .send((registration.clone(), SessionCollectionEvent::Added)));
        Ok(handle_for_client)
    }
}
