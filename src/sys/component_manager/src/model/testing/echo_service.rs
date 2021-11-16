// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        model::error::ModelError,
        model::hooks::*,
    },
    async_trait::async_trait,
    cm_rust::*,
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::endpoints::ServerEnd,
    fidl_fidl_examples_routing_echo::{EchoMarker, EchoRequest, EchoRequestStream},
    fuchsia_zircon::{self as zx},
    futures::TryStreamExt,
    lazy_static::lazy_static,
    routing::capability_source::InternalCapability,
    std::{
        convert::TryInto,
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref ECHO_CAPABILITY: CapabilityName = "builtin.Echo".try_into().unwrap();
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
        task_scope: TaskScope,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let server_end = ServerEnd::<EchoMarker>::new(server_end);
        let mut stream: EchoRequestStream = server_end.into_stream().unwrap();
        task_scope
            .add_task(async move {
                while let Some(EchoRequest::EchoString { value, responder }) =
                    stream.try_next().await.unwrap()
                {
                    responder.send(value.as_ref().map(|s| &**s)).unwrap();
                }
            })
            .await;
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
            InternalCapability::Protocol(name) if *name == *ECHO_CAPABILITY => {
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
            source: CapabilitySource::Builtin { capability, .. },
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
