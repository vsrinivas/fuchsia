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
    static ref SMC_RESOURCE_CAPABILITY_NAME: CapabilityName = "fuchsia.kernel.SmcResource".into();
}

/// An implementation of fuchsia.kernel.SmcResource protocol.
pub struct SmcResource {
    resource: Resource,
}

#[cfg(target_arch = "aarch64")]
impl SmcResource {
    /// `resource` must be the SMC resource.
    pub fn new(resource: Resource) -> Arc<Self> {
        Arc::new(Self { resource })
    }
}

#[async_trait]
impl ResourceCapability for SmcResource {
    const KIND: zx::sys::zx_rsrc_kind_t = zx::sys::ZX_RSRC_KIND_SMC;
    const NAME: &'static str = "SmcResource";
    type Marker = fkernel::SmcResourceMarker;

    fn get_resource_info(self: &Arc<Self>) -> Result<ResourceInfo, Error> {
        Ok(self.resource.info()?)
    }

    async fn server_loop(
        self: Arc<Self>,
        mut stream: <Self::Marker as ServiceMarker>::RequestStream,
    ) -> Result<(), Error> {
        while let Some(fkernel::SmcResourceRequest::Get { responder }) = stream.try_next().await? {
            responder.send(self.resource.duplicate_handle(zx::Rights::SAME_RIGHTS)?)?;
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&SMC_RESOURCE_CAPABILITY_NAME)
    }
}

#[cfg(all(test, target_arch = "aarch64"))]
mod tests {
    use {
        super::*,
        crate::{
            builtin::capability::BuiltinCapability,
            model::hooks::{Event, EventPayload, Hooks},
        },
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_kernel as fkernel, fuchsia_async as fasync,
        fuchsia_component::client::connect_to_service,
        fuchsia_zircon::AsHandleRef,
        fuchsia_zircon_sys as sys,
        futures::lock::Mutex,
        moniker::AbsoluteMoniker,
        std::path::PathBuf,
    };

    fn smc_resource_available() -> bool {
        let bin = std::env::args().next();
        match bin.as_ref().map(String::as_ref) {
            Some("/pkg/bin/component_manager_test") => false,
            Some("/pkg/bin/component_manager_boot_env_test") => true,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    async fn get_smc_resource() -> Result<Resource, Error> {
        let smc_resource_provider = connect_to_service::<fkernel::SmcResourceMarker>()?;
        let smc_resource_handle = smc_resource_provider.get().await?;
        Ok(Resource::from(smc_resource_handle))
    }

    async fn serve_smc_resource() -> Result<fkernel::SmcResourceProxy, Error> {
        let smc_resource = get_smc_resource().await?;

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fkernel::SmcResourceMarker>()?;
        fasync::Task::local(
            SmcResource::new(smc_resource)
                .serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving SMC resource service: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn fail_with_no_smc_resource() -> Result<(), Error> {
        if smc_resource_available() {
            return Ok(());
        }
        let (_, stream) = fidl::endpoints::create_proxy_and_stream::<fkernel::SmcResourceMarker>()?;
        assert!(!SmcResource::new(Resource::from(zx::Handle::invalid()))
            .serve(stream)
            .await
            .is_ok());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn kind_type_is_smc() -> Result<(), Error> {
        if !smc_resource_available() {
            return Ok(());
        }

        let smc_resource_provider = serve_smc_resource().await?;
        let smc_resource: Resource = smc_resource_provider.get().await?;
        let resource_info = smc_resource.info()?;
        assert_eq!(resource_info.kind, zx::sys::ZX_RSRC_KIND_SMC);
        assert_eq!(resource_info.base, 0);
        assert_eq!(resource_info.size, 0);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_connect_to_smc_service() -> Result<(), Error> {
        if !smc_resource_available() {
            return Ok(());
        }

        let smc_resource = SmcResource::new(get_smc_resource().await?);
        let hooks = Hooks::new(None);
        hooks.install(smc_resource.hooks()).await;

        let provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Builtin {
            capability: InternalCapability::Protocol(SMC_RESOURCE_CAPABILITY_NAME.clone()),
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

        let smc_client = ClientEnd::<fkernel::SmcResourceMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        let smc_resource = smc_client.get().await?;
        assert_ne!(smc_resource.raw_handle(), sys::ZX_HANDLE_INVALID);
        Ok(())
    }
}
