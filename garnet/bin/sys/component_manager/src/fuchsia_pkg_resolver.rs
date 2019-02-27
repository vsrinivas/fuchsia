// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io_util,
    crate::model::{Resolver, ResolverError},
    cm_fidl_translator,
    failure::{format_err, Error, ResultExt},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxy, UpdatePolicy},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_app::client::connect_to_service,
    fuchsia_uri::pkg_uri::FuchsiaPkgUri,
    fuchsia_zircon as zx,
    futures::future::FutureObj,
    std::path::PathBuf,
};

pub static SCHEME: &str = "fuchsia-pkg";

/// Resolves component URIs with the "fuchsia-pkg" scheme. See the fuchsia_pkg_uri crate for URI
/// syntax.
pub struct FuchsiaPkgResolver {
    pkg_resolver: PackageResolverProxy,
}

impl FuchsiaPkgResolver {
    pub fn new() -> Result<FuchsiaPkgResolver, Error> {
        let pkg_resolver = connect_to_service::<PackageResolverMarker>()
            .context("error connecting to package resolver")?;
        Ok(FuchsiaPkgResolver { pkg_resolver })
    }

    async fn resolve_async<'a>(
        &'a self,
        component_uri: &'a str,
    ) -> Result<fsys::Component, ResolverError> {
        // Parse URI.
        let fuchsia_pkg_uri = FuchsiaPkgUri::parse(component_uri)
            .map_err(|e| ResolverError::uri_parse_error(component_uri, e))?;
        fuchsia_pkg_uri.resource().ok_or(ResolverError::uri_missing_resource_error(
            component_uri,
        ))?;
        let cm_path: PathBuf = fuchsia_pkg_uri.resource().unwrap().into();

        // Resolve package.
        let (package_dir_c, package_dir_s) =
            zx::Channel::create().map_err(|e| ResolverError::component_not_available(component_uri, e))?;
        let selectors: [&str; 0] = [];
        let mut update_policy = UpdatePolicy { fetch_if_absent: true, allow_old_versions: false };
        let status = await!(self.pkg_resolver.resolve(
            component_uri,
            &mut selectors.iter().map(|s| *s),
            &mut update_policy,
            ServerEnd::new(package_dir_s)
        ))
        .map_err(|e| ResolverError::component_not_available(component_uri, e))?;
        let status = zx::Status::from_raw(status);
        if status != zx::Status::OK {
            return Err(ResolverError::component_not_available(component_uri, format_err!("{}", status)));
        }

        // Read component manifest from package.
        let dir = ClientEnd::<DirectoryMarker>::new(package_dir_c)
            .into_proxy()
            .expect("failed to create directory proxy");
        let file = io_util::open_file(&dir, &cm_path)
            .map_err(|e| ResolverError::manifest_not_available(component_uri, e))?;
        let cm_str = await!(io_util::read_file(&file))
            .map_err(|e| ResolverError::manifest_not_available(component_uri, e))?;
        let component_decl = cm_fidl_translator::translate(&cm_str)
            .map_err(|e| ResolverError::manifest_invalid(component_uri, e))?;
        let package_dir = ClientEnd::new(
            dir.into_channel().expect("could not convert proxy to channel").into_zx_channel(),
        );
        let package = fsys::Package {
            package_uri: Some(format!("{}", fuchsia_pkg_uri.root_uri())),
            package_dir: Some(package_dir),
        };
        Ok(fsys::Component {
            resolved_uri: Some(component_uri.to_string()),
            decl: Some(component_decl),
            package: Some(package),
        })
    }
}

impl Resolver for FuchsiaPkgResolver {
    fn resolve<'a>(
        &'a self,
        component_uri: &'a str,
    ) -> FutureObj<'a, Result<fsys::Component, ResolverError>> {
        FutureObj::new(Box::new(self.resolve_async(component_uri)))
    }
}
