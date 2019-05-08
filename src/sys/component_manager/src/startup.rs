// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        fuchsia_boot_resolver::{self, FuchsiaBootResolver},
        fuchsia_pkg_resolver::{self, FuchsiaPkgResolver},
        model::ResolverRegistry,
    },
    failure::{Error, ResultExt},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxy},
    fuchsia_component::client::connect_to_service,
    std::path::PathBuf,
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
