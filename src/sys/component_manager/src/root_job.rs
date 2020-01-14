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
    fuchsia_runtime::job_default,
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, prelude::*},
    log::warn,
    std::sync::{Arc, Weak},
};

/// An implementation of the `fuchsia.boot.RootJob` protocol.
pub struct RootJob {
    capability_path: CapabilityPath,
    rights: zx::Rights,
}

impl RootJob {
    pub fn new(capability_path: CapabilityPath, rights: zx::Rights) -> Arc<Self> {
        Arc::new(Self { capability_path, rights })
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "RootJob",
            vec![EventType::RouteCapability],
            Arc::downgrade(&self) as Weak<dyn Hook>,
        )]
    }

    /// Serves an instance of the `fuchsia.boot.RootJob` protocol given an appropriate
    /// RequestStream. Returns when the channel backing the RequestStream is closed or an
    /// unrecoverable error occurs.
    pub async fn serve(
        self: Arc<Self>,
        mut stream: fboot::RootJobRequestStream,
    ) -> Result<(), Error> {
        let job = job_default();
        while let Some(fboot::RootJobRequest::Get { responder }) = stream.try_next().await? {
            responder.send(job.duplicate(self.rights)?)?;
        }
        Ok(())
    }

    async fn on_route_framework_capability_async<'a>(
        self: &'a Arc<Self>,
        capability: &'a FrameworkCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match capability {
            FrameworkCapability::Protocol(capability_path)
                if *capability_path == self.capability_path =>
            {
                Ok(Some(Box::new(RootJobCapabilityProvider::new(Arc::downgrade(&self)))
                    as Box<dyn CapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }
}

impl Hook for RootJob {
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

struct RootJobCapabilityProvider {
    root_job: Weak<RootJob>,
}

impl RootJobCapabilityProvider {
    pub fn new(root_job: Weak<RootJob>) -> Self {
        Self { root_job }
    }
}

#[async_trait]
impl CapabilityProvider for RootJobCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = ServerEnd::<fboot::RootJobMarker>::new(server_end);
        let stream: fboot::RootJobRequestStream = server_end.into_stream().unwrap();
        fasync::spawn(async move {
            if let Some(root_job) = self.root_job.upgrade() {
                if let Err(err) = root_job.serve(stream).await {
                    warn!("RootJob::open failed: {}", err);
                }
            } else {
                warn!("RootJob has been dropped");
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
        fuchsia_zircon::AsHandleRef,
        fuchsia_zircon_sys as sys,
        futures::lock::Mutex,
        lazy_static::lazy_static,
        std::convert::TryInto,
    };

    lazy_static! {
        pub static ref CAPABILITY_PATH: CapabilityPath =
            "/svc/fuchsia.boot.RootJob".try_into().unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn has_correct_rights() -> Result<(), Error> {
        let root_job = RootJob::new(CAPABILITY_PATH.clone(), zx::Rights::TRANSFER);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fboot::RootJobMarker>()?;
        fasync::spawn_local(
            root_job.serve(stream).unwrap_or_else(|err| panic!("Error serving root job: {}", err)),
        );

        let root_job = proxy.get().await?;
        let info = zx::Handle::from(root_job).basic_info()?;
        assert_eq!(info.rights, zx::Rights::TRANSFER);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_connect() -> Result<(), Error> {
        let root_job = RootJob::new(CAPABILITY_PATH.clone(), zx::Rights::SAME_RIGHTS);
        let hooks = Hooks::new(None);
        hooks.install(root_job.hooks()).await;

        let provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Framework {
            capability: FrameworkCapability::Protocol(CAPABILITY_PATH.clone()),
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

        let client = ClientEnd::<fboot::RootJobMarker>::new(client)
            .into_proxy()
            .expect("Failed to create proxy");
        let handle = client.get().await?;
        assert_ne!(handle.raw_handle(), sys::ZX_HANDLE_INVALID);
        Ok(())
    }
}
