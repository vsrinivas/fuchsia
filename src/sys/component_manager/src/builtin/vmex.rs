// Copyright 2019 The Fuchsia Authors. All rights reserved.
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
    fidl_fuchsia_boot as fboot, fidl_fuchsia_security_resource as fsec, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{future::BoxFuture, prelude::*},
    lazy_static::lazy_static,
    log::warn,
    std::{
        convert::TryInto,
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref VMEX_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.process.Vmex".try_into().unwrap();
}

/// An implementation of fuchsia.security.resource.Vmex protocol.
pub struct VmexService {
    inner: Arc<VmexServiceInner>,
}

impl VmexService {
    pub fn new() -> Self {
        Self { inner: Arc::new(VmexServiceInner::new()) }
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "VmexService",
            vec![EventType::RouteCapability],
            Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        )]
    }

    /// Serves an instance of the 'fuchsia.security.resource.Vmex' protocol given an appropriate
    /// RequestStream. Returns when the channel backing the RequestStream is closed or an
    /// unrecoverable error, like failure to acquire the root resource occurs.
    pub async fn serve(mut stream: fsec::VmexRequestStream) -> Result<(), Error> {
        let root_resource_provider = connect_to_service::<fboot::RootResourceMarker>()?;
        let root_resource = root_resource_provider.get().await?;

        while let Some(fsec::VmexRequest::Get { responder }) = stream.try_next().await? {
            let vmex_handle =
                root_resource.create_child(zx::ResourceKind::VMEX, None, 0, 0, b"vmex")?;
            let restricted_vmex_handle = vmex_handle.replace_handle(
                zx::Rights::TRANSFER | zx::Rights::DUPLICATE | zx::Rights::INSPECT,
            )?;
            responder.send(zx::Resource::from(restricted_vmex_handle))?;
        }
        Ok(())
    }
}

struct VmexServiceInner;

impl VmexServiceInner {
    pub fn new() -> Self {
        Self {}
    }

    async fn on_route_framework_capability_async<'a>(
        self: Arc<Self>,
        capability: &'a FrameworkCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match capability {
            FrameworkCapability::Protocol(capability_path)
                if *capability_path == *VMEX_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(VmexCapabilityProvider::new()) as Box<dyn CapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }
}

impl Hook for VmexServiceInner {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            if let EventPayload::RouteCapability {
                source: CapabilitySource::Framework { capability, scope_moniker: None },
                capability_provider,
            } = &event.payload
            {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_route_framework_capability_async(&capability, capability_provider.take())
                    .await?;
            };
            Ok(())
        })
    }
}

struct VmexCapabilityProvider;

impl VmexCapabilityProvider {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl CapabilityProvider for VmexCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = ServerEnd::<fsec::VmexMarker>::new(server_end);
        let stream: fsec::VmexRequestStream = server_end.into_stream().unwrap();
        fasync::spawn(async move {
            let result = VmexService::serve(stream).await;
            if let Err(e) = result {
                warn!("VmexService.open failed: {}", e);
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
    };

    fn root_resource_available() -> bool {
        let bin = std::env::args().next();
        match bin.as_ref().map(String::as_ref) {
            Some("/pkg/test/component_manager_tests") => false,
            Some("/pkg/test/component_manager_boot_env_tests") => true,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    fn serve_vmex() -> Result<fsec::VmexProxy, Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fsec::VmexMarker>()?;
        fasync::spawn_local(
            VmexService::serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving vmex service: {}", e)),
        );
        Ok(proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn fail_with_no_root_resource() -> Result<(), Error> {
        if root_resource_available() {
            return Ok(());
        }
        let (_, stream) = fidl::endpoints::create_proxy_and_stream::<fsec::VmexMarker>()?;
        assert!(!VmexService::serve(stream).await.is_ok());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn kind_type_is_vmex() -> Result<(), Error> {
        if !root_resource_available() {
            return Ok(());
        }

        let vmex_provider = serve_vmex()?;
        let vmex_resource = vmex_provider.get().await?;
        let resource_info = vmex_resource.info()?;
        assert_eq!(resource_info.kind, zx::sys::ZX_RSRC_KIND_VMEX);
        assert_eq!(resource_info.base, 0);
        assert_eq!(resource_info.size, 0);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn minimal_rights_assigned() -> Result<(), Error> {
        if !root_resource_available() {
            return Ok(());
        }

        let vmex_provider = serve_vmex()?;
        let vmex_resource = vmex_provider.get().await?;
        let resource_info = zx::Handle::from(vmex_resource).basic_info()?;
        assert_eq!(
            resource_info.rights,
            zx::Rights::DUPLICATE | zx::Rights::TRANSFER | zx::Rights::INSPECT
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn connect_to_vmex_service() -> Result<(), Error> {
        if !root_resource_available() {
            return Ok(());
        }

        let vmex_service = Arc::new(VmexService::new());
        let hooks = Hooks::new(None);
        hooks.install(vmex_service.hooks()).await;

        let capability_provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Framework {
            capability: FrameworkCapability::Protocol(VMEX_CAPABILITY_PATH.clone()),
            scope_moniker: None,
        };

        let (client, server) = zx::Channel::create()?;

        let event = Event::new(
            AbsoluteMoniker::root(),
            EventPayload::RouteCapability {
                source,
                capability_provider: capability_provider.clone(),
            },
        );
        hooks.dispatch(&event).await?;

        let capability_provider = capability_provider.lock().await.take();
        if let Some(capability_provider) = capability_provider {
            capability_provider.open(0, 0, PathBuf::new(), server).await?;
        }

        let vmex_client = ClientEnd::<fsec::VmexMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        let vmex_resource = vmex_client.get().await?;
        assert_ne!(vmex_resource.raw_handle(), sys::ZX_HANDLE_INVALID);
        Ok(())
    }
}
