// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::capability::BuiltinCapability,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl_fuchsia_kernel as fkernel,
    fuchsia_zircon::{self as zx, HandleBased, Resource},
    futures::prelude::*,
    lazy_static::lazy_static,
    routing::capability_source::InternalCapability,
    std::sync::Arc,
};

lazy_static! {
    static ref CPU_RESOURCE_CAPABILITY_NAME: CapabilityName = "fuchsia.kernel.CpuResource".into();
}

/// An implementation of fuchsia.kernel.CpuResource protocol.
pub struct CpuResource {
    resource: Resource,
}

impl CpuResource {
    /// `resource` must be the Cpu resource.
    pub fn new(resource: Resource) -> Result<Arc<Self>, Error> {
        let resource_info = resource.info()?;
        if resource_info.kind != zx::sys::ZX_RSRC_KIND_SYSTEM
            || resource_info.base != zx::sys::ZX_RSRC_SYSTEM_CPU_BASE
            || resource_info.size != 1
        {
            return Err(format_err!("CPU resource not available."));
        }
        Ok(Arc::new(Self { resource }))
    }
}

#[async_trait]
impl BuiltinCapability for CpuResource {
    const NAME: &'static str = "CpuResource";
    type Marker = fkernel::CpuResourceMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: fkernel::CpuResourceRequestStream,
    ) -> Result<(), Error> {
        while let Some(fkernel::CpuResourceRequest::Get { responder }) = stream.try_next().await? {
            responder.send(self.resource.duplicate_handle(zx::Rights::SAME_RIGHTS)?)?;
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&CPU_RESOURCE_CAPABILITY_NAME)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            builtin::capability::BuiltinCapability,
            capability::CapabilitySource,
            model::hooks::{Event, EventPayload, Hooks},
        },
        cm_task_scope::TaskScope,
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_io as fio, fidl_fuchsia_kernel as fkernel, fuchsia_async as fasync,
        fuchsia_component::client::connect_to_protocol,
        fuchsia_zircon::sys,
        fuchsia_zircon::AsHandleRef,
        futures::lock::Mutex,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
        std::path::PathBuf,
        std::sync::Weak,
    };

    fn cpu_resource_available() -> bool {
        let bin = std::env::args().next();
        match bin.as_ref().map(String::as_ref) {
            Some("/pkg/bin/component_manager_test") => false,
            Some("/pkg/bin/component_manager_boot_env_test") => true,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    async fn get_cpu_resource() -> Result<Resource, Error> {
        let cpu_resource_provider = connect_to_protocol::<fkernel::CpuResourceMarker>()?;
        let cpu_resource_handle = cpu_resource_provider.get().await?;
        Ok(Resource::from(cpu_resource_handle))
    }

    async fn serve_cpu_resource() -> Result<fkernel::CpuResourceProxy, Error> {
        let cpu_resource = get_cpu_resource().await?;

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fkernel::CpuResourceMarker>()?;
        fasync::Task::local(
            CpuResource::new(cpu_resource)
                .unwrap_or_else(|e| panic!("Error while creating CPU resource service: {}", e))
                .serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving CPU resource service: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    #[fuchsia::test]
    async fn fail_with_no_cpu_resource() -> Result<(), Error> {
        if cpu_resource_available() {
            return Ok(());
        }
        assert!(!CpuResource::new(Resource::from(zx::Handle::invalid())).is_ok());
        Ok(())
    }

    #[fuchsia::test]
    async fn base_type_is_cpu() -> Result<(), Error> {
        if !cpu_resource_available() {
            return Ok(());
        }

        let cpu_resource_provider = serve_cpu_resource().await?;
        let cpu_resource: Resource = cpu_resource_provider.get().await?;
        let resource_info = cpu_resource.info()?;
        assert_eq!(resource_info.kind, zx::sys::ZX_RSRC_KIND_SYSTEM);
        assert_eq!(resource_info.base, zx::sys::ZX_RSRC_SYSTEM_CPU_BASE);
        assert_eq!(resource_info.size, 1);
        Ok(())
    }

    #[fuchsia::test]
    async fn can_connect_to_cpu_service() -> Result<(), Error> {
        if !cpu_resource_available() {
            return Ok(());
        }

        let cpu_resource = CpuResource::new(get_cpu_resource().await?).unwrap();
        let hooks = Hooks::new();
        hooks.install(cpu_resource.hooks()).await;

        let provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Builtin {
            capability: InternalCapability::Protocol(CPU_RESOURCE_CAPABILITY_NAME.clone()),
            top_instance: Weak::new(),
        };

        let event = Event::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRouted { source, capability_provider: provider.clone() }),
        );
        hooks.dispatch(&event).await?;

        let (client, mut server) = zx::Channel::create()?;
        let task_scope = TaskScope::new();
        if let Some(provider) = provider.lock().await.take() {
            provider
                .open(task_scope.clone(), fio::OpenFlags::empty(), 0, PathBuf::new(), &mut server)
                .await?;
        };

        let cpu_client = ClientEnd::<fkernel::CpuResourceMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        let cpu_resource = cpu_client.get().await?;
        assert_ne!(cpu_resource.raw_handle(), sys::ZX_HANDLE_INVALID);
        Ok(())
    }
}
