// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        fuchsia_boot_resolver::{self, FuchsiaBootResolver},
        fuchsia_pkg_resolver::{self, FuchsiaPkgResolver},
        model::{error::ModelError, hub::Hub, ModelParams, ResolverRegistry},
    },
    failure::{Error, ResultExt},
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_io::{NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxy},
    fuchsia_component::client::connect_to_service,
    fuchsia_runtime::HandleType,
    fuchsia_vfs_pseudo_fs::directory::{self, entry::DirectoryEntry},
    std::{iter, path::PathBuf, sync::Arc},
};

pub fn available_resolvers() -> Result<ResolverRegistry, Error> {
    let mut resolver_registry = ResolverRegistry::new();
    resolver_registry
        .register(fuchsia_boot_resolver::SCHEME.to_string(), Box::new(FuchsiaBootResolver::new()));

    // Add the fuchsia-pkg resolver to the registry if it's available.
    if let Some(pkg_resolver) = connect_pkg_resolver()? {
        resolver_registry.register(
            fuchsia_pkg_resolver::SCHEME.to_string(),
            Box::new(FuchsiaPkgResolver::new(pkg_resolver)),
        );
    }

    Ok(resolver_registry)
}

/// Checks if a package resolver service is available through our namespace and connects to it if
/// so. If not availble, returns Ok(None).
fn connect_pkg_resolver() -> Result<Option<PackageResolverProxy>, Error> {
    let service_path = PathBuf::from(format!("/svc/{}", PackageResolverMarker::NAME));
    if !service_path.exists() {
        return Ok(None);
    }

    let pkg_resolver = connect_to_service::<PackageResolverMarker>()
        .context("error connecting to package resolver")?;
    return Ok(Some(pkg_resolver));
}

/// Installs a Hub if possible.
pub fn install_hub_if_possible(model_params: &mut ModelParams) -> Result<(), ModelError> {
    if let Some(out_dir_handle) =
        fuchsia_runtime::take_startup_handle(HandleType::DirectoryRequest.into())
    {
        let mut root_directory = directory::simple::empty();
        root_directory.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(out_dir_handle.into()),
        );
        model_params
            .hooks
            .push(Arc::new(Hub::new(model_params.root_component_url.clone(), root_directory)?));
    };
    Ok(())
}
