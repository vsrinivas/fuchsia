// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::service::ServiceEvent;
use crate::session::Session;
use crate::session_id_rights;
use crate::Result;
use failure::ResultExt;
use fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker};
use fidl_fuchsia_mediasession::{
    PublisherMarker, PublisherRequest, PublisherRequestStream, SessionMarker,
};
use fuchsia_app::server::ServiceFactory;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc::Sender,
    {SinkExt, TryFutureExt, TryStreamExt},
};
use zx::AsHandleRef;

/// `Publisher` implements fuchsia.media.session.Publisher.
#[derive(Clone)]
pub struct Publisher {
    fidl_sink: Sender<ServiceEvent>,
}

impl Publisher {
    pub fn factory(fidl_sink: Sender<ServiceEvent>) -> impl ServiceFactory {
        let publisher = Publisher { fidl_sink };
        (PublisherMarker::NAME, move |channel| {
            fasync::spawn(publisher.clone().serve(channel).unwrap_or_else(|e| eprintln!("{}", e)))
        })
    }

    async fn serve(mut self, channel: fasync::Channel) -> Result<()> {
        let mut request_stream = PublisherRequestStream::from_channel(channel);
        while let Some(request) =
            await!(request_stream.try_next()).context("Publisher server request stream.")?
        {
            let PublisherRequest::Publish { responder, controller } = request;
            responder
                .send(await!(self.publish(controller))?)
                .context("Giving session id to client.")?;
        }
        Ok(())
    }

    async fn publish(&mut self, controller: ClientEnd<SessionMarker>) -> Result<zx::Event> {
        let event = zx::Event::create()?;
        let session_id: zx::Event = event.as_handle_ref().duplicate(session_id_rights())?.into();
        let id = event.as_handle_ref().get_koid()?;
        let registration = ServiceEvent::NewSession((
            Session::new(
                id,
                controller.into_proxy().context("Making controller client end into proxy.")?,
                self.fidl_sink.clone(),
            )?,
            event,
        ));
        await!(self.fidl_sink.send(registration))?;
        Ok(session_id)
    }
}
