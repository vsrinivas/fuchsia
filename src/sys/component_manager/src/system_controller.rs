// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::*,
        model::{error::ModelError, hooks::*},
    },
    cm_rust::CapabilityPath,
    failure::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2::*,
    fuchsia_async::{self as fasync},
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, prelude::*},
    lazy_static::lazy_static,
    log::warn,
    std::{
        convert::TryInto,
        process,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref SYSTEM_CONTROLLER_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.sys2.SystemController".try_into().unwrap();
}

#[derive(Clone)]
pub struct SystemController {
    inner: Arc<SystemControllerInner>,
}

impl SystemController {
    pub fn new() -> Self {
        Self { inner: Arc::new(SystemControllerInner::new()) }
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::RouteBuiltinCapability],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }
}

struct SystemControllerInner;

impl SystemControllerInner {
    pub fn new() -> Self {
        Self {}
    }

    async fn on_route_builtin_capability_async<'a>(
        self: Arc<Self>,
        capability: &'a ComponentManagerCapability,
        capability_provider: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        match capability {
            ComponentManagerCapability::LegacyService(capability_path)
                if *capability_path == *SYSTEM_CONTROLLER_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(SystemControllerCapabilityProvider::new())
                    as Box<dyn ComponentManagerCapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }
}

impl Hook for SystemControllerInner {
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

struct SystemControllerCapabilityProvider;

impl SystemControllerCapabilityProvider {
    pub fn new() -> Self {
        Self {}
    }

    async fn open_async(mut stream: SystemControllerRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                SystemControllerRequest::Shutdown { responder } => {
                    match responder.send() {
                        Ok(()) => {}
                        Err(e) => {
                            println!(
                                "error sending response to shutdown requester:\
                                 {}\n shut down proceeding",
                                e
                            );
                        }
                    }
                    // TODO(jmatt) replace with a call into the model or something to actually stop
                    // everthing
                    process::exit(0);
                }
            }
        }
        Ok(())
    }
}

impl ComponentManagerCapabilityProvider for SystemControllerCapabilityProvider {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        let server_end = ServerEnd::<SystemControllerMarker>::new(server_end);
        let stream: SystemControllerRequestStream = server_end.into_stream().unwrap();
        fasync::spawn(async move {
            let result = Self::open_async(stream).await;
            if let Err(e) = result {
                warn!("SystemController.open failed: {}", e);
            }
        });

        Box::pin(async { Ok(()) })
    }
}
