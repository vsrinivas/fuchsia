// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hanging_get::{
    error::HangingGetServerError,
    server::{Handle, HangingGetBroker, Publisher, DEFAULT_CHANNEL_SIZE},
};
use {
    fidl_fuchsia_bluetooth_component::{
        LifecycleGetStateResponder, LifecycleRequest, LifecycleRequestStream, LifecycleState,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::StreamExt,
};

/// `ComponentLifecycleServer` handles a single, hanging-get based request, `GetState` for the
/// Lifecycle protocol. It responds to client requests and can be used to update the current
/// Lifecycle state.
#[derive(Clone)]
pub struct ComponentLifecycleServer {
    inner: Publisher<LifecycleState>,
    handle: Handle<LifecycleGetStateResponder>,
}

impl ComponentLifecycleServer {
    /// Spawn an async task to handle updates and return a new `ComponentLifecycleServer`.
    pub fn spawn() -> Self {
        let broker = HangingGetBroker::new(
            LifecycleState::Initializing,
            |s, o: LifecycleGetStateResponder| {
                let _ = o.send(*s);
            },
            DEFAULT_CHANNEL_SIZE,
        );
        let inner = broker.new_publisher();
        let handle = broker.new_handle();
        fasync::spawn(broker.run());
        Self { inner, handle }
    }

    /// Return a `FnMut` that can be passed to a `ServiceFs` to handle FIDL service requests for
    /// the Lifecycle protocol. The returned function value is tied to this
    /// `ComponentLifecycleServer` instance and will respect any modifications made by the `set`
    /// method.
    pub fn fidl_service(&self) -> impl FnMut(LifecycleRequestStream) {
        let handle = self.handle.clone();
        move |mut stream: LifecycleRequestStream| {
            let mut handle = handle.clone();
            fasync::spawn(async move {
                if let Ok(mut subscriber) = handle.new_subscriber().await {
                    while let Some(request) = stream.next().await {
                        match request {
                            Ok(LifecycleRequest::GetState { responder }) => {
                                let _ = subscriber.register(responder).await;
                            }
                            Err(e) => fx_log_err!("Error handing client request: {}", e),
                        }
                    }
                }
            });
        }
    }

    /// Set the `LifecycleState` that this server will report and update all hanging-get clients
    /// with the new value.
    pub async fn set(&mut self, state: LifecycleState) -> Result<(), HangingGetServerError> {
        self.inner.set(state).await
    }
}
