// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hanging_get::{
    asynchronous::{HangingGetBroker, Publisher, SubscriptionRegistrar, DEFAULT_CHANNEL_SIZE},
    error::HangingGetServerError,
};
use {
    fidl_fuchsia_bluetooth_component::{
        LifecycleGetStateResponder, LifecycleRequest, LifecycleRequestStream, LifecycleState,
    },
    fuchsia_async as fasync,
    futures::StreamExt,
    log::error,
};

/// `ComponentLifecycleServer` handles a single, hanging-get based request, `GetState` for the
/// Lifecycle protocol. It responds to client requests and can be used to update the current
/// Lifecycle state.
#[derive(Clone)]
pub struct ComponentLifecycleServer {
    publisher: Publisher<LifecycleState>,
    registrar: SubscriptionRegistrar<LifecycleGetStateResponder>,
}

impl ComponentLifecycleServer {
    /// Spawn an async task to handle updates and return a new `ComponentLifecycleServer`.
    pub fn spawn() -> Self {
        let broker = HangingGetBroker::new(
            LifecycleState::Initializing,
            |s, o: LifecycleGetStateResponder| o.send(*s).is_ok(),
            DEFAULT_CHANNEL_SIZE,
        );
        let publisher = broker.new_publisher();
        let registrar = broker.new_registrar();
        fasync::Task::spawn(broker.run()).detach();
        Self { publisher, registrar }
    }

    /// Return a `FnMut` that can be passed to a `ServiceFs` to handle FIDL service requests for
    /// the Lifecycle protocol. The returned function value is tied to this
    /// `ComponentLifecycleServer` instance and will respect any modifications made by the `set`
    /// method.
    pub fn fidl_service(&self) -> impl FnMut(LifecycleRequestStream) {
        let registrar = self.registrar.clone();
        move |mut stream: LifecycleRequestStream| {
            let mut registrar = registrar.clone();
            fasync::Task::spawn(async move {
                if let Ok(mut subscriber) = registrar.new_subscriber().await {
                    while let Some(request) = stream.next().await {
                        match request {
                            Ok(LifecycleRequest::GetState { responder }) => {
                                let _ = subscriber.register(responder).await;
                            }
                            Err(e) => error!("Error handing client request: {}", e),
                        }
                    }
                }
            })
            .detach();
        }
    }

    /// Set the `LifecycleState` that this server will report and update all hanging-get clients
    /// with the new value.
    pub async fn set(&mut self, state: LifecycleState) -> Result<(), HangingGetServerError> {
        self.publisher.set(state).await
    }
}
