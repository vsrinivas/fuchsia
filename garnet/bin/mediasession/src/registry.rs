// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::service::ServiceEvent;
use failure::ResultExt;
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_mediasession::{RegistryMarker, RegistryRequest};
use fuchsia_app::server::ServiceFactory;
use fuchsia_async as fasync;
use fuchsia_zircon::AsHandleRef;
use futures::channel::mpsc::Sender;
use futures::{SinkExt, TryStreamExt};

/// `Registry` implements `fuchsia.media.session.Registry`.
#[derive(Clone)]
pub struct Registry {
    fidl_sink: Sender<ServiceEvent>,
}

impl Registry {
    pub fn factory(fidl_sink: Sender<ServiceEvent>) -> impl ServiceFactory {
        let controller_registry = Registry { fidl_sink };
        (RegistryMarker::NAME, move |channel| {
            fasync::spawn(controller_registry.clone().serve(channel))
        })
    }

    async fn serve(mut self, channel: fasync::Channel) {
        let (mut request_stream, control_handle) =
            trylog!(ServerEnd::<RegistryMarker>::new(channel.into_zx_channel())
                .into_stream_and_control_handle());
        trylog!(await!(self
            .fidl_sink
            .send(ServiceEvent::NewActiveSessionChangeListener(control_handle))));
        while let Some(request) =
            trylog!(await!(request_stream.try_next()).context("Registry server request stream."))
        {
            let RegistryRequest::ConnectToSessionById { session_id, controller_request, .. } =
                request;

            let koid = trylog!(session_id.as_handle_ref().get_koid());
            trylog!(await!(self.fidl_sink.send(ServiceEvent::NewSessionRequest {
                session_id: koid,
                request: controller_request
            })));
        }
    }
}
