// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::*,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    cm_rust::CapabilityPath,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_boot as fboot, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, Handle, HandleBased, Resource},
    futures::{future::BoxFuture, prelude::*},
    lazy_static::lazy_static,
    log::warn,
    std::{
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref ROOT_RESOURCE_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.boot.RootResource".try_into().unwrap();
}

/// An implementation of the `fuchsia.boot.RootResource` protocol.
pub struct RootResource {
    resource: Resource,
}

impl RootResource {
    pub fn new(handle: Handle) -> Arc<Self> {
        Arc::new(Self { resource: Resource::from(handle) })
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::RouteCapability],
            callback: Arc::downgrade(&self) as Weak<dyn Hook>,
        }]
    }

    /// Serves an instance of the `fuchsia.boot.RootResource` protocol given an appropriate
    /// RequestStream. Returns when the channel backing the RequestStream is closed or an
    /// unrecoverable error occurs.
    pub async fn serve(
        self: Arc<Self>,
        mut stream: fboot::RootResourceRequestStream,
    ) -> Result<(), Error> {
        while let Some(fboot::RootResourceRequest::Get { responder }) = stream.try_next().await? {
            responder.send(self.resource.duplicate_handle(zx::Rights::SAME_RIGHTS)?)?;
        }
        Ok(())
    }

    async fn on_route_framework_capability_async<'a>(
        self: &'a Arc<Self>,
        capability: &'a FrameworkCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match capability {
            FrameworkCapability::ServiceProtocol(capability_path)
                if *capability_path == *ROOT_RESOURCE_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(RootResourceCapabilityProvider::new(Arc::downgrade(&self)))
                    as Box<dyn CapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }
}

impl Hook for RootResource {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            if let EventPayload::RouteCapability {
                source: CapabilitySource::Framework { capability, scope_moniker: None },
                capability_provider,
            } = &event.payload
            {
                let mut provider = capability_provider.lock().await;
                *provider =
                    self.on_route_framework_capability_async(&capability, provider.take()).await?;
            };
            Ok(())
        })
    }
}

struct RootResourceCapabilityProvider {
    root_resource: Weak<RootResource>,
}

impl RootResourceCapabilityProvider {
    pub fn new(root_resource: Weak<RootResource>) -> Self {
        Self { root_resource }
    }
}

#[async_trait]
impl CapabilityProvider for RootResourceCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = ServerEnd::<fboot::RootResourceMarker>::new(server_end);
        let stream: fboot::RootResourceRequestStream = server_end.into_stream().unwrap();
        fasync::spawn(async move {
            if let Some(root_resource) = self.root_resource.upgrade() {
                if let Err(err) = root_resource.serve(stream).await {
                    warn!("RootResource::open failed: {}", err);
                }
            } else {
                warn!("RootResource has been dropped");
            }
        });
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{hooks::Hooks, moniker::AbsoluteMoniker},
        fidl::endpoints::ClientEnd,
        fuchsia_async as fasync,
        futures::lock::Mutex,
    };

    #[fasync::run_singlethreaded(test)]
    async fn can_connect() -> Result<(), Error> {
        let root_resource = RootResource::new(Handle::invalid());
        let hooks = Hooks::new(None);
        hooks.install(root_resource.hooks()).await;

        let provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Framework {
            capability: FrameworkCapability::ServiceProtocol(ROOT_RESOURCE_CAPABILITY_PATH.clone()),
            scope_moniker: None,
        };

        let event = Event::new(
            AbsoluteMoniker::root(),
            EventPayload::RouteCapability { source, capability_provider: provider.clone() },
        );
        hooks.dispatch(&event).await?;

        let (client, server) = zx::Channel::create()?;
        if let Some(provider) = provider.lock().await.take() {
            provider.open(0, 0, String::new(), server).await?;
        }

        // We do not call get, as we passed an invalid handle to RootResource,
        // which would cause a PEER_CLOSED failure. We passed an invalid handle
        // to RootResource because you need a Resource to create another one,
        // which we do not have.
        ClientEnd::<fboot::RootJobMarker>::new(client)
            .into_proxy()
            .expect("Failed to create proxy");
        Ok(())
    }
}
