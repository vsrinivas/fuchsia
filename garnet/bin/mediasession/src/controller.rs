// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::service::ServiceEvent;
use failure::ResultExt;
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_mediasession::{ControllerRegistryMarker, ControllerRegistryRequest};
use fuchsia_app::server::ServiceFactory;
use fuchsia_async as fasync;
use futures::channel::mpsc::Sender;
use futures::{SinkExt, TryStreamExt};

/// `ControllerRegistry` implements `fuchsia.media.session.ControllerRegistry`.
#[derive(Clone)]
pub struct ControllerRegistry {
    fidl_sink: Sender<ServiceEvent>,
}

impl ControllerRegistry {
    pub fn factory(fidl_sink: Sender<ServiceEvent>) -> impl ServiceFactory {
        let controller_registry = ControllerRegistry { fidl_sink };
        (ControllerRegistryMarker::NAME, move |channel| {
            fasync::spawn(controller_registry.clone().serve(channel))
        })
    }

    async fn serve(mut self, channel: fasync::Channel) {
        let (mut request_stream, control_handle) =
            trylog!(ServerEnd::<ControllerRegistryMarker>::new(channel.into_zx_channel())
                .into_stream_and_control_handle());
        trylog!(await!(self
            .fidl_sink
            .send(ServiceEvent::NewActiveSessionChangeListener(control_handle))));
        while let Some(request) =
            trylog!(await!(request_stream.try_next())
                .context("ControllerRegistry server request stream."))
        {
            let ControllerRegistryRequest::ConnectToControllerById {
                session_id,
                controller_request,
                ..
            } = request;

            trylog!(await!(self.fidl_sink.send(ServiceEvent::NewControllerRequest {
                session_id,
                request: controller_request
            })));
        }
    }
}
