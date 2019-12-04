// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{capability::*, model::hooks::*, model::*},
    cm_rust::*,
    fidl::endpoints::ServerEnd,
    fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream},
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx},
    futures::future::BoxFuture,
    futures::prelude::*,
    lazy_static::lazy_static,
    std::{
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref ECHO_CAPABILITY_PATH: CapabilityPath = "/svc/builtin.Echo".try_into().unwrap();
}

struct EchoCapabilityProvider;

impl EchoCapabilityProvider {
    pub fn new() -> Self {
        Self {}
    }
}

impl ComponentManagerCapabilityProvider for EchoCapabilityProvider {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        let server_end = ServerEnd::<EchoMarker>::new(server_end);
        let mut stream: EchoRequestStream = server_end.into_stream().unwrap();
        fasync::spawn(async move {
            while let Some(EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.unwrap()
            {
                responder.send(value.as_ref().map(|s| &**s)).unwrap();
            }
        });

        Box::pin(async { Ok(()) })
    }
}

pub struct EchoService {
    inner: Arc<EchoServiceInner>,
}

impl EchoService {
    pub fn new() -> Self {
        Self { inner: Arc::new(EchoServiceInner::new()) }
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::RouteBuiltinCapability],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }
}

struct EchoServiceInner;

impl EchoServiceInner {
    pub fn new() -> Self {
        Self {}
    }

    async fn on_route_builtin_capability_async<'a>(
        self: Arc<Self>,
        capability: &'a ComponentManagerCapability,
        capability_provider: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        println!("Capability_path: {:?}", capability.path());
        match capability {
            ComponentManagerCapability::ServiceProtocol(capability_path)
                if *capability_path == *ECHO_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(EchoCapabilityProvider::new())
                    as Box<dyn ComponentManagerCapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }
}

impl Hook for EchoServiceInner {
    fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(async move {
            match &event.payload {
                EventPayload::RouteBuiltinCapability { capability, capability_provider } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_builtin_capability_async(&capability, capability_provider.take())
                        .await?;
                }
                _ => {}
            };
            Ok(())
        })
    }
}
