// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::service::ServiceEvent;
use failure::ResultExt;
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_mediasession::{ControllerVendorMarker, ControllerVendorRequest};
use fuchsia_app::server::ServiceFactory;
use fuchsia_async as fasync;
use futures::channel::mpsc::Sender;
use futures::{SinkExt, TryStreamExt};

/// `ControllerVendor` implements `fuchsia.media.session.ControllerVendor`.
#[derive(Clone)]
pub struct ControllerVendor {
    fidl_sink: Sender<ServiceEvent>,
}

impl ControllerVendor {
    pub fn factory(fidl_sink: Sender<ServiceEvent>) -> impl ServiceFactory {
        let controller_vendor = ControllerVendor { fidl_sink };
        (ControllerVendorMarker::NAME, move |channel| {
            fasync::spawn(controller_vendor.clone().serve(channel))
        })
    }

    async fn serve(mut self, channel: fasync::Channel) {
        let (mut request_stream, control_handle) = trylog!(
            ServerEnd::<ControllerVendorMarker>::new(channel.into_zx_channel())
                .into_stream_and_control_handle()
        );
        trylog!(await!(self.fidl_sink.send(
            ServiceEvent::NewActiveSessionChangeListener(control_handle)
        )));
        while let Some(request) =
            trylog!(await!(request_stream.try_next())
                .context("ControllerVendor server request stream."))
        {
            let ControllerVendorRequest::ConnectToControllerById {
                session_id,
                controller_request,
                ..
            } = request;

            trylog!(await!(self.fidl_sink.send(
                ServiceEvent::NewControllerRequest {
                    session_id,
                    request: controller_request
                }
            )));
        }
    }
}
