// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{capability::*, channel, model::error::ModelError, model::hooks::*},
    async_trait::async_trait,
    cm_rust::*,
    fidl::endpoints::ServerEnd,
    fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream},
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx},
    futures::prelude::*,
    lazy_static::lazy_static,
    std::{
        convert::TryInto,
        path::PathBuf,
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

#[async_trait]
impl CapabilityProvider for EchoCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let server_end = ServerEnd::<EchoMarker>::new(server_end);
        let mut stream: EchoRequestStream = server_end.into_stream().unwrap();
        fasync::Task::spawn(async move {
            while let Some(EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.unwrap()
            {
                responder.send(value.as_ref().map(|s| &**s)).unwrap();
            }
        })
        .detach();

        Ok(())
    }
}

pub struct EchoService;

impl EchoService {
    pub fn new() -> Self {
        Self
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "EchoService",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_framework_capability_routed_async<'a>(
        self: Arc<Self>,
        capability: &'a InternalCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match capability {
            InternalCapability::Protocol(CapabilityNameOrPath::Path(capability_path))
                if *capability_path == *ECHO_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(EchoCapabilityProvider::new()) as Box<dyn CapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }
}

#[async_trait]
impl Hook for EchoService {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source: CapabilitySource::Builtin { capability },
            capability_provider,
        }) = &event.result
        {
            let mut capability_provider = capability_provider.lock().await;
            *capability_provider = self
                .on_framework_capability_routed_async(&capability, capability_provider.take())
                .await?;
        };
        Ok(())
    }
}
