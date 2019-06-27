// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    clone_session_id_handle, mpmc,
    proxies::session::{Session, SessionCollectionEvent, SessionRegistration},
    state::active_session_queue::ActiveSessionQueue,
    state::session_list::SessionList,
    Ref, Result,
};
use failure::ResultExt;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_media_sessions::{PublisherRequest, PublisherRequestStream, SessionMarker};
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use std::{ops::DerefMut, rc::Rc};
use zx::AsHandleRef;

/// `Publisher` implements fuchsia.media.session.Publisher.
#[derive(Clone)]
pub struct Publisher {
    session_list: Ref<SessionList>,
    active_session_queue: Ref<ActiveSessionQueue>,
    collection_event_sink: mpmc::Sender<(SessionRegistration, SessionCollectionEvent)>,
    active_session_sink: mpmc::Sender<Option<SessionRegistration>>,
}

impl Publisher {
    pub fn new(
        session_list: Ref<SessionList>,
        active_session_queue: Ref<ActiveSessionQueue>,
        collection_event_sink: mpmc::Sender<(SessionRegistration, SessionCollectionEvent)>,
        active_session_sink: mpmc::Sender<Option<SessionRegistration>>,
    ) -> Publisher {
        Publisher { session_list, active_session_queue, collection_event_sink, active_session_sink }
    }

    pub async fn serve(mut self, mut request_stream: PublisherRequestStream) -> Result<()> {
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
        let registration = SessionRegistration { id: Rc::new(session_id_handle), koid, is_local };
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
