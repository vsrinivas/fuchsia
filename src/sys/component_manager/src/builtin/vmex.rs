// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::*,
        channel,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_security_resource as fsec, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased, Resource},
    futures::prelude::*,
    lazy_static::lazy_static,
    log::warn,
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref VMEX_CAPABILITY_NAME: CapabilityName = "fuchsia.security.resource.Vmex".into();
}

/// An implementation of fuchsia.security.resource.Vmex protocol.
pub struct VmexService {
    resource: Resource,
}

impl VmexService {
    /// `resource` must be the root resource.
    pub fn new(resource: Resource) -> Arc<Self> {
        Arc::new(Self { resource })
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "VmexService",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Serves an instance of the 'fuchsia.security.resource.Vmex' protocol given an appropriate
    /// RequestStream. Returns when the channel backing the RequestStream is closed or an
    /// unrecoverable error, like failure to acquire the root resource occurs.
    pub async fn serve(self: Arc<Self>, mut stream: fsec::VmexRequestStream) -> Result<(), Error> {
        let vmex_handle =
            self.resource.create_child(zx::ResourceKind::VMEX, None, 0, 0, b"vmex")?;
        while let Some(fsec::VmexRequest::Get { responder }) = stream.try_next().await? {
            let restricted_vmex_handle = vmex_handle.duplicate_handle(
                zx::Rights::TRANSFER | zx::Rights::DUPLICATE | zx::Rights::INSPECT,
            )?;
            responder.send(zx::Resource::from(restricted_vmex_handle))?;
        }
        Ok(())
    }

    async fn on_framework_capability_routed_async<'a>(
        self: Arc<Self>,
        capability: &'a InternalCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        if capability.matches_protocol(&VMEX_CAPABILITY_NAME) {
            Ok(Some(Box::new(VmexCapabilityProvider::new(self)) as Box<dyn CapabilityProvider>))
        } else {
            Ok(capability_provider)
        }
    }
}

#[async_trait]
impl Hook for VmexService {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source: CapabilitySource::AboveRoot { capability },
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

struct VmexCapabilityProvider {
    vmex_service: Arc<VmexService>,
}

impl VmexCapabilityProvider {
    pub fn new(vmex_service: Arc<VmexService>) -> Self {
        Self { vmex_service: vmex_service }
    }
}

#[async_trait]
impl CapabilityProvider for VmexCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let server_end = ServerEnd::<fsec::VmexMarker>::new(server_end);
        let stream: fsec::VmexRequestStream = server_end.into_stream().unwrap();
        fasync::Task::spawn(async move {
            let result = self.vmex_service.serve(stream).await;
            if let Err(e) = result {
                warn!("VmexService.serve failed: {}", e);
            }
        })
        .detach();

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{hooks::Hooks, moniker::AbsoluteMoniker},
        cm_rust::CapabilityNameOrPath,
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_boot as fboot, fuchsia_async as fasync,
        fuchsia_component::client::connect_to_service,
        fuchsia_zircon::AsHandleRef,
        fuchsia_zircon_sys as sys,
        futures::lock::Mutex,
    };

    fn root_resource_available() -> bool {
        let bin = std::env::args().next();
        match bin.as_ref().map(String::as_ref) {
            Some("/pkg/bin/component_manager_test") => false,
            Some("/pkg/bin/component_manager_boot_env_test") => true,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    async fn get_root_resource() -> Result<Resource, Error> {
        let root_resource_provider = connect_to_service::<fboot::RootResourceMarker>()?;
        let root_resource_handle = root_resource_provider.get().await?;
        Ok(Resource::from(root_resource_handle))
    }

    async fn serve_vmex() -> Result<fsec::VmexProxy, Error> {
        let root_resource = get_root_resource().await?;

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fsec::VmexMarker>()?;
        fasync::Task::local(
            VmexService::new(root_resource)
                .serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving vmex service: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn fail_with_no_root_resource() -> Result<(), Error> {
        if root_resource_available() {
            return Ok(());
        }
        let (_, stream) = fidl::endpoints::create_proxy_and_stream::<fsec::VmexMarker>()?;
        assert!(!VmexService::new(Resource::from(zx::Handle::invalid()))
            .serve(stream)
            .await
            .is_ok());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn kind_type_is_vmex() -> Result<(), Error> {
        if !root_resource_available() {
            return Ok(());
        }

        let vmex_provider = serve_vmex().await?;
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

        let vmex_provider = serve_vmex().await?;
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

        let root_resource = get_root_resource().await?;
        let vmex_service = Arc::new(VmexService::new(root_resource));
        let hooks = Hooks::new(None);
        hooks.install(vmex_service.hooks()).await;

        let capability_provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::AboveRoot {
            capability: InternalCapability::Protocol(CapabilityNameOrPath::Name(
                VMEX_CAPABILITY_NAME.clone(),
            )),
        };

        let (client, mut server) = zx::Channel::create()?;

        let event = Event::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRouted {
                source,
                capability_provider: capability_provider.clone(),
            }),
        );
        hooks.dispatch(&event).await?;

        let capability_provider = capability_provider.lock().await.take();
        if let Some(capability_provider) = capability_provider {
            capability_provider.open(0, 0, PathBuf::new(), &mut server).await?;
        }

        let vmex_client = ClientEnd::<fsec::VmexMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        let vmex_resource = vmex_client.get().await?;
        assert_ne!(vmex_resource.raw_handle(), sys::ZX_HANDLE_INVALID);
        Ok(())
    }
}
