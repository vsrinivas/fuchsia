// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::capability::BuiltinCapability,
    ::routing::capability_source::InternalCapability,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl_fuchsia_kernel as fkernel,
    fuchsia_zircon::{self as zx, HandleBased, Resource},
    futures::prelude::*,
    lazy_static::lazy_static,
    std::sync::Arc,
};

lazy_static! {
    static ref INFO_RESOURCE_CAPABILITY_NAME: CapabilityName = "fuchsia.kernel.InfoResource".into();
}

/// An implementation of fuchsia.kernel.InfoResource protocol.
pub struct InfoResource {
    resource: Resource,
}

impl InfoResource {
    /// `resource` must be the Info resource.
    pub fn new(resource: Resource) -> Result<Arc<Self>, Error> {
        let resource_info = resource.info()?;
        if resource_info.kind != zx::sys::ZX_RSRC_KIND_SYSTEM
            || resource_info.base != zx::sys::ZX_RSRC_SYSTEM_INFO_BASE
            || resource_info.size != 1
        {
            return Err(format_err!("Info resource not available."));
        }
        Ok(Arc::new(Self { resource }))
    }
}

#[async_trait]
impl BuiltinCapability for InfoResource {
    const NAME: &'static str = "InfoResource";
    type Marker = fkernel::InfoResourceMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: fkernel::InfoResourceRequestStream,
    ) -> Result<(), Error> {
        while let Some(fkernel::InfoResourceRequest::Get { responder }) = stream.try_next().await? {
            responder.send(self.resource.duplicate_handle(zx::Rights::SAME_RIGHTS)?)?;
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&INFO_RESOURCE_CAPABILITY_NAME)
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

    fn info_resource_available() -> bool {
        let bin = std::env::args().next();
        match bin.as_ref().map(String::as_ref) {
            Some("/pkg/bin/component_manager_test") => false,
            Some("/pkg/bin/component_manager_boot_env_test") => true,
            _ => panic!("Unexpected test binary name {:?}", bin),
        }
    }

    async fn get_info_resource() -> Result<Resource, Error> {
        let info_resource_provider = connect_to_protocol::<fkernel::InfoResourceMarker>()?;
        let info_resource_handle = info_resource_provider.get().await?;
        Ok(Resource::from(info_resource_handle))
    }

    async fn serve_info_resource() -> Result<fkernel::InfoResourceProxy, Error> {
        let info_resource = get_info_resource().await?;

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fkernel::InfoResourceMarker>()?;
        fasync::Task::local(
            InfoResource::new(info_resource)
                .unwrap_or_else(|e| panic!("Error while creating info resource service: {}", e))
                .serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving INFO resource service: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    #[fuchsia::test]
    async fn fail_with_no_info_resource() -> Result<(), Error> {
        if info_resource_available() {
            return Ok(());
        }
        assert!(!InfoResource::new(Resource::from(zx::Handle::invalid())).is_ok());
        Ok(())
    }

    #[fuchsia::test]
    async fn base_type_is_info() -> Result<(), Error> {
        if !info_resource_available() {
            return Ok(());
        }

        let info_resource_provider = serve_info_resource().await?;
        let info_resource: Resource = info_resource_provider.get().await?;
        let resource_info = info_resource.info()?;
        assert_eq!(resource_info.kind, zx::sys::ZX_RSRC_KIND_SYSTEM);
        assert_eq!(resource_info.base, zx::sys::ZX_RSRC_SYSTEM_INFO_BASE);
        assert_eq!(resource_info.size, 1);
        Ok(())
    }

    #[fuchsia::test]
    async fn can_connect_to_info_service() -> Result<(), Error> {
        if !info_resource_available() {
            return Ok(());
        }

        let info_resource = InfoResource::new(get_info_resource().await?).unwrap();
        let hooks = Hooks::new();
        hooks.install(info_resource.hooks()).await;

        let provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Builtin {
            capability: InternalCapability::Protocol(INFO_RESOURCE_CAPABILITY_NAME.clone()),
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

        let info_client = ClientEnd::<fkernel::InfoResourceMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        let info_resource = info_client.get().await?;
        assert_ne!(info_resource.raw_handle(), sys::ZX_HANDLE_INVALID);
        Ok(())
    }
}
