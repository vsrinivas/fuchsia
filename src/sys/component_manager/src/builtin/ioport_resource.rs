// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{builtin::capability::ResourceCapability, capability::*},
    anyhow::Error,
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_kernel as fkernel,
    fuchsia_zircon::{self as zx, HandleBased, Resource, ResourceInfo},
    futures::prelude::*,
    lazy_static::lazy_static,
    std::sync::Arc,
};

lazy_static! {
    static ref IOPORT_RESOURCE_CAPABILITY_NAME: CapabilityName =
        "fuchsia.kernel.IoportResource".into();
}

/// An implementation of fuchsia.kernel.IoportResource protocol.
pub struct IoportResource {
    resource: Resource,
}

#[cfg(target_arch = "x86_64")]
impl IoportResource {
    /// `resource` must be the IOPORT resource.
    pub fn new(resource: Resource) -> Arc<Self> {
        Arc::new(Self { resource })
    }
}

#[async_trait]
impl ResourceCapability for IoportResource {
    const KIND: zx::sys::zx_rsrc_kind_t = zx::sys::ZX_RSRC_KIND_IOPORT;
    const NAME: &'static str = "IoportResource";
    type Marker = fkernel::IoportResourceMarker;

    fn get_resource_info(self: &Arc<Self>) -> Result<ResourceInfo, Error> {
        Ok(self.resource.info()?)
    }

    async fn server_loop(
        self: Arc<Self>,
        mut stream: <Self::Marker as ServiceMarker>::RequestStream,
    ) -> Result<(), Error> {
        while let Some(fkernel::IoportResourceRequest::Get { responder }) =
            stream.try_next().await?
        {
            responder.send(self.resource.duplicate_handle(zx::Rights::SAME_RIGHTS)?)?;
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&IOPORT_RESOURCE_CAPABILITY_NAME)
    }
}

#[cfg(all(test, target_arch = "x86_64"))]
mod tests {
    use {
        super::*,
        crate::{
            builtin::capability::BuiltinCapability,
            model::{
                hooks::{Event, EventPayload, Hooks},
                moniker::AbsoluteMoniker,
            },
        },
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_kernel as fkernel, fuchsia_async as fasync,
        fuchsia_component::client::connect_to_service,
        fuchsia_zircon::AsHandleRef,
        fuchsia_zircon_sys as sys,
        futures::lock::Mutex,
        std::path::PathBuf,
    };

    fn ioport_resource_available() -> bool {
        let bin = std::env::args().next();
        match bin.as_ref().map(String::as_ref) {
            Some("/pkg/bin/component_manager_test") => false,
            Some("/pkg/bin/component_manager_boot_env_test") => true,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    async fn get_ioport_resource() -> Result<Resource, Error> {
        let ioport_resource_provider = connect_to_service::<fkernel::IoportResourceMarker>()?;
        let ioport_resource_handle = ioport_resource_provider.get().await?;
        Ok(Resource::from(ioport_resource_handle))
    }

    async fn serve_ioport_resource() -> Result<fkernel::IoportResourceProxy, Error> {
        let ioport_resource = get_ioport_resource().await?;

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fkernel::IoportResourceMarker>()?;
        fasync::Task::local(
            IoportResource::new(ioport_resource)
                .serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving IOPORT resource service: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn fail_with_no_ioport_resource() -> Result<(), Error> {
        if ioport_resource_available() {
            return Ok(());
        }
        let (_, stream) =
            fidl::endpoints::create_proxy_and_stream::<fkernel::IoportResourceMarker>()?;
        assert!(!IoportResource::new(Resource::from(zx::Handle::invalid()))
            .serve(stream)
            .await
            .is_ok());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn kind_type_is_ioport() -> Result<(), Error> {
        if !ioport_resource_available() {
            return Ok(());
        }

        let ioport_resource_provider = serve_ioport_resource().await?;
        let ioport_resource: Resource = ioport_resource_provider.get().await?;
        let resource_info = ioport_resource.info()?;
        assert_eq!(resource_info.kind, zx::sys::ZX_RSRC_KIND_IOPORT);
        assert_eq!(resource_info.base, 0);
        assert_eq!(resource_info.size, 0);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_connect_to_ioport_service() -> Result<(), Error> {
        if !ioport_resource_available() {
            return Ok(());
        }

        let ioport_resource = IoportResource::new(get_ioport_resource().await?);
        let hooks = Hooks::new(None);
        hooks.install(ioport_resource.hooks()).await;

        let provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Builtin {
            capability: InternalCapability::Protocol(IOPORT_RESOURCE_CAPABILITY_NAME.clone()),
        };

        let event = Event::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRouted { source, capability_provider: provider.clone() }),
        );
        hooks.dispatch(&event).await?;

        let (client, mut server) = zx::Channel::create()?;
        if let Some(provider) = provider.lock().await.take() {
            provider.open(0, 0, PathBuf::new(), &mut server).await?;
        }

        let ioport_client = ClientEnd::<fkernel::IoportResourceMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        let ioport_resource = ioport_client.get().await?;
        assert_ne!(ioport_resource.raw_handle(), sys::ZX_HANDLE_INVALID);
        Ok(())
    }
}
