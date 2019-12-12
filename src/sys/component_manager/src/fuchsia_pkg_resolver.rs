// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::resolver::{Resolver, ResolverError, ResolverFut},
    cm_fidl_translator,
    failure::format_err,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys::LoaderProxy,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::pkg_url::PkgUrl,
    std::path::Path,
};

#[allow(unused)]
pub static SCHEME: &str = "fuchsia-pkg";

/// Resolves component URLs with the "fuchsia-pkg" scheme. See the fuchsia_pkg_url crate for URL
/// syntax.
pub struct FuchsiaPkgResolver {
    loader: LoaderProxy,
}

impl FuchsiaPkgResolver {
    pub fn new(loader: LoaderProxy) -> FuchsiaPkgResolver {
        FuchsiaPkgResolver { loader }
    }

    async fn resolve_async<'a>(
        &'a self,
        component_url: &'a str,
    ) -> Result<fsys::Component, ResolverError> {
        // Parse URL.
        let fuchsia_pkg_url = PkgUrl::parse(component_url)
            .map_err(|e| ResolverError::url_parse_error(component_url, e))?;
        let cm_path = Path::new(
            fuchsia_pkg_url
                .resource()
                .ok_or(ResolverError::url_missing_resource_error(component_url))?,
        );
        let package_url = fuchsia_pkg_url.root_url().to_string();

        // Resolve package.
        let package = self
            .loader
            .load_url(&package_url)
            .await
            .map_err(|e| ResolverError::component_not_available(component_url, e))?
            .ok_or(ResolverError::component_not_available(
                component_url,
                format_err!("package not available"),
            ))?;
        let dir = package.directory.ok_or(ResolverError::component_not_available(
            component_url,
            format_err!("package is missing directory handle"),
        ))?;

        // Read component manifest from package.
        let dir = ClientEnd::<DirectoryMarker>::new(dir)
            .into_proxy()
            .expect("failed to create directory proxy");
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
        let package =
            fsys::Package { package_url: Some(package_url), package_dir: Some(package_dir) };
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
        cm_fidl_translator,
        fidl::endpoints::{self, ServerEnd},
        fidl_fuchsia_data as fdata,
        fidl_fuchsia_sys::{LoaderMarker, LoaderRequest, Package},
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::TryStreamExt,
        std::path::Path,
    };

    struct MockLoader {}

    impl MockLoader {
        fn start() -> LoaderProxy {
            let (proxy, server): (_, ServerEnd<LoaderMarker>) = endpoints::create_proxy().unwrap();
            fasync::spawn_local(async move {
                let loader = MockLoader {};
                let mut stream = server.into_stream().unwrap();
                while let Some(LoaderRequest::LoadUrl { url, responder }) =
                    stream.try_next().await.expect("failed to read request")
                {
                    let mut package = loader.load_url(&url);
                    let package = package.as_mut();
                    responder.send(package).expect("responder failed");
                }
            });
            proxy
        }

        fn load_url(&self, package_url: &str) -> Option<Package> {
            let (dir_c, dir_s) = zx::Channel::create().unwrap();
            let parsed_url = PkgUrl::parse(&package_url).expect("bad url");
            // Simulate a package server that only contains the "hello_world" package.
            if parsed_url.name().unwrap() != "hello_world" {
                return None;
            }
            let path = Path::new("/pkg");
            io_util::connect_in_namespace(
                path.to_str().unwrap(),
                dir_s,
                io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
            )
            .expect("could not connect to /pkg");
            Some(Package {
                data: None,
                directory: Some(dir_c),
                resolved_url: package_url.to_string(),
            })
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_test() {
        let loader = MockLoader::start();
        let resolver = FuchsiaPkgResolver::new(loader);
        let url = "fuchsia-pkg://fuchsia.com/hello_world#\
                   meta/component_manager_tests_hello_world.cm";
        let component = resolver.resolve_async(url).await.expect("resolve failed");

        // Check that both the returned component manifest and the component manifest in
        // the returned package dir match the expected value. This also tests that
        // the resolver returned the right package dir.
        let fsys::Component { resolved_url, decl, package } = component;
        assert_eq!(resolved_url.unwrap(), url);
        let program = fdata::Dictionary {
            entries: vec![fdata::Entry {
                key: "binary".to_string(),
                value: Some(Box::new(fdata::Value::Str("bin/hello_world".to_string()))),
            }],
        };
        let expected_decl = fsys::ComponentDecl {
            program: Some(program),
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
        let loader = MockLoader::start();
        let resolver = FuchsiaPkgResolver::new(loader);
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
            ComponentNotAvailable
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
