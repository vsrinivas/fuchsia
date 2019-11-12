// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        find_exposed_root_directory_capability, Model, Resolver, ResolverError, ResolverFut,
    },
    cm_fidl_translator,
    failure::format_err,
    fidl::endpoints::{create_proxy, ClientEnd},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::pkg_url::PkgUrl,
    futures::lock::Mutex,
    std::convert::TryInto,
    std::path::{Path, PathBuf},
    std::sync::{Arc, Weak},
};

pub static SCHEME: &str = "fuchsia-pkg";

/// Resolves component URLs with the "fuchsia-pkg" scheme. See the fuchsia_pkg_url crate for URL
/// syntax. Uses raw pkgfs handle, and thus can only load packages from the "base" package set.
/// The root component must expose "/pkgfs" directory capability for this to work.
pub struct FuchsiaPkgResolver {
    model: Arc<Mutex<Option<Weak<Model>>>>,
    pkgfs_handle: Mutex<Option<DirectoryProxy>>,
}

impl FuchsiaPkgResolver {
    pub fn new(model: Arc<Mutex<Option<Weak<Model>>>>) -> FuchsiaPkgResolver {
        FuchsiaPkgResolver { model, pkgfs_handle: Mutex::new(None) }
    }

    async fn resolve_package(
        &self,
        component_package_url: &PkgUrl,
    ) -> Result<DirectoryProxy, ResolverError> {
        let mut o_pkgfs_handle = self.pkgfs_handle.lock().await;
        if o_pkgfs_handle.is_none() {
            // If we don't currently have a handle to pkgfs...
            // - perform capability routing
            // - bind to the appropriate component
            // - open the pkgfs directory capability
            // - and store the opened handle in self.pkgfs_handle.
            let model_guard = self.model.lock().await;
            let model = model_guard.as_ref().expect("model reference missing");
            let model = model.upgrade().ok_or(ResolverError::model_not_available())?;
            let (capability_path, realm) =
                find_exposed_root_directory_capability(&model, "/pkgfs".try_into().unwrap())
                    .await
                    .map_err(|e| {
                        ResolverError::component_not_available(
                            component_package_url.to_string(),
                            format_err!("failed to route pkgfs handle: {}", e),
                        )
                    })?;
            let (pkgfs_client, pkgfs_server) = create_proxy::<DirectoryMarker>().map_err(|e| {
                ResolverError::component_not_available(component_package_url.to_string(), e)
            })?;
            model
                .bind_instance_open_outgoing(
                    realm,
                    OPEN_RIGHT_READABLE,
                    MODE_TYPE_DIRECTORY,
                    &capability_path,
                    pkgfs_server.into_channel(),
                )
                .await
                .map_err(|e| {
                    ResolverError::component_not_available(
                        component_package_url.to_string(),
                        format_err!("failed to bind to pkgfs provider: {}", e),
                    )
                })?;
            *o_pkgfs_handle = Some(pkgfs_client)
        }
        // The system package is available at the `system` path inside of pkgfs, whereas all other
        // packages are available at `packages/$PACKAGE_NAME/0`.
        let p = match io_util::canonicalize_path(component_package_url.root_url().path()) {
            "system" => PathBuf::from("system"),
            path => PathBuf::from("packages").join(path).join("0"),
        };
        let dir =
            io_util::open_directory(o_pkgfs_handle.as_ref().unwrap(), &p, OPEN_RIGHT_READABLE)
                .map_err(|e| {
                    ResolverError::component_not_available(
                        component_package_url.to_string(),
                        e.context("failed to open package directory"),
                    )
                })?;
        Ok(dir)
    }

    async fn resolve_async<'a>(
        &'a self,
        component_url: &'a str,
    ) -> Result<fsys::Component, ResolverError> {
        // Parse URL.
        let component_package_url = PkgUrl::parse(component_url)
            .map_err(|e| ResolverError::url_parse_error(component_url, e))?;
        let cm_path = Path::new(
            component_package_url
                .resource()
                .ok_or(ResolverError::url_missing_resource_error(component_url))?,
        );

        // Resolve package.
        let dir = self.resolve_package(&component_package_url).await?;

        // Read component manifest from package.
        let file = io_util::open_file(&dir, cm_path, io_util::OPEN_RIGHT_READABLE)
            .map_err(|e| ResolverError::manifest_not_available(component_url, e))?;
        let cm_str = io_util::read_file(&file)
            .await
            .map_err(|e| ResolverError::manifest_not_available(component_url, e))?;
        let component_decl = cm_fidl_translator::translate(&cm_str)
            .map_err(|e| ResolverError::manifest_invalid(component_url, e))?;
        let package_dir = ClientEnd::new(
            dir.into_channel().expect("could not convert proxy to channel").into_zx_channel(),
        );
        let package = fsys::Package {
            package_url: Some(component_package_url.root_url().to_string()),
            package_dir: Some(package_dir),
        };
        Ok(fsys::Component {
            resolved_url: Some(component_url.to_string()),
            decl: Some(component_decl),
            package: Some(package),
        })
    }
}

impl Resolver for FuchsiaPkgResolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut {
        Box::pin(self.resolve_async(component_url))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_fidl_translator, directory_broker,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_data as fdata,
        fidl_fuchsia_io::NodeMarker,
        fuchsia_async as fasync,
        fuchsia_vfs_pseudo_fs::{directory::entry::DirectoryEntry, pseudo_directory},
        std::{iter, path::Path},
    };

    fn new_fake_pkgfs() -> DirectoryProxy {
        let (pkgfs_client, pkgfs_server) = create_proxy::<DirectoryMarker>().unwrap();

        fasync::spawn_local(async move {
            let packages_dir_broker_fn =
                |_, _, relative_path: String, server_end: ServerEnd<NodeMarker>| {
                    if relative_path.is_empty() {
                        // We don't support this in this fake, drop the server_end
                        return;
                    }
                    let path = PathBuf::from(relative_path.clone());
                    let mut path_iter = path.iter();

                    // We're simulating a package server that only contains the "hello_world" package.
                    if path_iter.next().unwrap().to_str().unwrap() != "hello_world" {
                        return;
                    }

                    // The next item is 0, as per pkgfs semantics. Check it and skip it.
                    assert_eq!("0", path_iter.next().unwrap().to_str().unwrap());

                    let mut open_path = PathBuf::from("/pkg");
                    open_path.extend(path_iter);
                    io_util::connect_in_namespace(
                        open_path.to_str().unwrap(),
                        server_end.into_channel(),
                        OPEN_RIGHT_READABLE,
                    )
                    .expect("failed to open path in namespace");
                };
            let mut fake_pkgfs = pseudo_directory! {
                "packages" =>
                    directory_broker::DirectoryBroker::new(Box::new(packages_dir_broker_fn)),
            };
            fake_pkgfs.open(
                OPEN_RIGHT_READABLE,
                MODE_TYPE_DIRECTORY,
                &mut iter::empty(),
                ServerEnd::new(pkgfs_server.into_channel()),
            );
            let _ = fake_pkgfs.await;
            panic!("fake_pkgfs exited!");
        });
        pkgfs_client
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_test() {
        let resolver = FuchsiaPkgResolver {
            model: Arc::new(Mutex::new(None)),
            pkgfs_handle: Mutex::new(Some(new_fake_pkgfs())),
        };
        let url = "fuchsia-pkg://fuchsia.com/hello_world#\
                   meta/component_manager_tests_hello_world.cm";
        let component = resolver.resolve_async(url).await.expect("resolve failed");

        // Check that both the returned component manifest and the component manifest in
        // the returned package dir match the expected value. This also tests that
        // the resolver returned the right package dir.
        let fsys::Component { resolved_url, decl, package } = component;
        assert_eq!(resolved_url.unwrap(), url);
        let expected_decl = fsys::ComponentDecl {
            program: Some(fdata::Dictionary {
                entries: vec![fdata::Entry {
                    key: "binary".to_string(),
                    value: Some(Box::new(fdata::Value::Str("bin/hello_world".to_string()))),
                }],
            }),
            uses: None,
            exposes: None,
            offers: None,
            facets: None,
            children: None,
            collections: None,
            storage: None,
            runners: None,
        };
        assert_eq!(decl.unwrap(), expected_decl);

        let fsys::Package { package_url, package_dir } = package.unwrap();
        assert_eq!(package_url.unwrap(), "fuchsia-pkg://fuchsia.com/hello_world");
        let dir_proxy = package_dir.unwrap().into_proxy().unwrap();
        let path = Path::new("meta/component_manager_tests_hello_world.cm");
        let file_proxy = io_util::open_file(&dir_proxy, path, io_util::OPEN_RIGHT_READABLE)
            .expect("could not open cm");
        let cm_contents = io_util::read_file(&file_proxy).await.expect("could not read cm");
        assert_eq!(
            cm_fidl_translator::translate(&cm_contents).expect("could not parse cm"),
            expected_decl
        );
    }

    macro_rules! test_resolve_error {
        ($resolver:ident, $url:expr, $resolver_error_expected:ident) => {
            let url = $url;
            let res = $resolver.resolve_async(url).await;
            match res.err().expect("unexpected success") {
                ResolverError::$resolver_error_expected { url: u, .. } => {
                    assert_eq!(u, url);
                }
                e => panic!("unexpected error {:?}", e),
            }
        };
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_errors_test() {
        let resolver = FuchsiaPkgResolver {
            model: Arc::new(Mutex::new(None)),
            pkgfs_handle: Mutex::new(Some(new_fake_pkgfs())),
        };
        test_resolve_error!(
            resolver,
            "fuchsia-pkg:///hello_world#meta/component_manager_tests_hello_world.cm",
            UrlParseError
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello_world",
            UrlMissingResourceError
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/goodbye_world#\
             meta/component_manager_tests_hello_world.cm",
            ManifestNotAvailable
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello_world#meta/does_not_exist.cm",
            ManifestNotAvailable
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello_world#data/component_manager_tests_invalid.cm",
            ManifestInvalid
        );
    }
}
