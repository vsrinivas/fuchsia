// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_boot as fboot, fidl_fuchsia_security_resource as fsec,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::prelude::*,
};

/// An implementation of fuchsia.security.resource.Vmex protocol.
pub struct VmexService;

impl VmexService {
    /// Serves an instance of the 'fuchsia.security.resource.Vmex' protocol given an appropriate
    /// RequestStream. Returns when the channel backing the RequestStream is closed or an
    /// unrecoverable error, like failure to acquire the root resource occurs.
    pub async fn serve(mut stream: fsec::VmexRequestStream) -> Result<(), Error> {
        let root_resource_provider = connect_to_service::<fboot::RootResourceMarker>()?;
        let root_resource = root_resource_provider.get().await?;

        while let Some(fsec::VmexRequest::Get { responder }) = stream.try_next().await? {
            let vmex_handle =
                root_resource.create_child(zx::sys::ZX_RSRC_KIND_VMEX, 0, 0, b"vmex")?;
            let restricted_vmex_handle = vmex_handle.replace_handle(
                zx::Rights::TRANSFER | zx::Rights::DUPLICATE | zx::Rights::INSPECT,
            )?;
            responder.send(zx::Resource::from(restricted_vmex_handle))?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, fuchsia_zircon::AsHandleRef};

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
}
