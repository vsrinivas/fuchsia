// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{Resolver, ResolverError},
    cm_fidl_translator,
    failure::format_err,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{PackageResolverProxy, UpdatePolicy},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_uri::pkg_uri::PkgUri,
    fuchsia_zircon as zx,
    futures::future::FutureObj,
    std::path::PathBuf,
};

pub static SCHEME: &str = "fuchsia-pkg";

/// Resolves component URLs with the "fuchsia-pkg" scheme. See the fuchsia_pkg_uri crate for URL
/// syntax.
pub struct FuchsiaPkgResolver {
    pkg_resolver: PackageResolverProxy,
}

impl FuchsiaPkgResolver {
    pub fn new(pkg_resolver: PackageResolverProxy) -> FuchsiaPkgResolver {
        FuchsiaPkgResolver { pkg_resolver }
    }

    async fn resolve_async<'a>(
        &'a self,
        component_url: &'a str,
    ) -> Result<fsys::Component, ResolverError> {
        // Parse URL.
        let fuchsia_pkg_uri = PkgUri::parse(component_url)
            .map_err(|e| ResolverError::url_parse_error(component_url, e))?;
        fuchsia_pkg_uri
            .resource()
            .ok_or(ResolverError::url_missing_resource_error(component_url))?;
        let package_url = fuchsia_pkg_uri.root_uri().to_string();
        let cm_path: PathBuf = fuchsia_pkg_uri.resource().unwrap().into();

        // Resolve package.
        let (package_dir_c, package_dir_s) = zx::Channel::create()
            .map_err(|e| ResolverError::component_not_available(component_url, e))?;
        let selectors: [&str; 0] = [];
        let mut update_policy = UpdatePolicy { fetch_if_absent: true, allow_old_versions: false };
        let status = await!(self.pkg_resolver.resolve(
            &package_url,
            &mut selectors.iter().map(|s| *s),
            &mut update_policy,
            ServerEnd::new(package_dir_s)
        ))
        .map_err(|e| ResolverError::component_not_available(component_url, e))?;
        let status = zx::Status::from_raw(status);
        if status != zx::Status::OK {
            return Err(ResolverError::component_not_available(
                component_url,
                format_err!("{}", status),
            ));
        }

        // Read component manifest from package.
        let dir = ClientEnd::<DirectoryMarker>::new(package_dir_c)
            .into_proxy()
            .expect("failed to create directory proxy");
        let file = io_util::open_file(&dir, &cm_path)
            .map_err(|e| ResolverError::manifest_not_available(component_url, e))?;
        let cm_str = await!(io_util::read_file(&file))
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
    fn resolve<'a>(
        &'a self,
        component_url: &'a str,
    ) -> FutureObj<'a, Result<fsys::Component, ResolverError>> {
        FutureObj::new(Box::new(self.resolve_async(component_url)))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_fidl_translator,
        fidl::endpoints,
        fidl_fuchsia_data as fdata,
        fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverRequest},
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::TryStreamExt,
        std::path::Path,
    };

    struct MockPackageResolver {}

    impl MockPackageResolver {
        fn start() -> PackageResolverProxy {
            let (proxy, server): (_, ServerEnd<PackageResolverMarker>) =
                endpoints::create_proxy().unwrap();
            fasync::spawn_local(async move {
                let pkg_resolver = MockPackageResolver {};
                let mut stream = server.into_stream().unwrap();
                while let Some(PackageResolverRequest::Resolve {
                    package_uri,
                    dir,
                    responder,
                    ..
                }) = await!(stream.try_next()).expect("failed to read request")
                {
                    let s = match pkg_resolver.resolve(&package_uri, dir) {
                        Ok(()) => 0,
                        Err(s) => s.into_raw(),
                    };
                    responder.send(s).expect("responder failed");
                }
            });
            proxy
        }

        fn resolve(
            &self,
            package_url: &str,
            dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
        ) -> Result<(), zx::Status> {
            let package_url = PkgUri::parse(&package_url).expect("bad url");
            if package_url.name().unwrap() != "hello_world" {
                return Err(zx::Status::NOT_FOUND);
            }
            let path = Path::new("/pkg");
            io_util::connect_in_namespace(path.to_str().unwrap(), dir.into_channel())
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_test() {
        let pkg_resolver = MockPackageResolver::start();
        let resolver = FuchsiaPkgResolver::new(pkg_resolver);
        let url = "fuchsia-pkg://fuchsia.com/hello_world#\
                   meta/component_manager_tests_hello_world.cm";
        let component = await!(resolver.resolve_async(url)).expect("resolve failed");

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
            storage: None,
        };
        assert_eq!(decl.unwrap(), expected_decl);

        let fsys::Package { package_url, package_dir } = package.unwrap();
        assert_eq!(package_url.unwrap(), "fuchsia-pkg://fuchsia.com/hello_world");
        let dir_proxy = package_dir.unwrap().into_proxy().unwrap();
        let path = PathBuf::from("meta/component_manager_tests_hello_world.cm");
        let file_proxy = io_util::open_file(&dir_proxy, &path).expect("could not open cm");
        let cm_contents = await!(io_util::read_file(&file_proxy)).expect("could not read cm");
        assert_eq!(
            cm_fidl_translator::translate(&cm_contents).expect("could not parse cm"),
            expected_decl
        );
    }

    macro_rules! test_resolve_error {
        ($resolver:ident, $url:expr, $resolver_error_expected:ident) => {
            let url = $url;
            let res = await!($resolver.resolve_async(url));
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
        let pkg_resolver = MockPackageResolver::start();
        let resolver = FuchsiaPkgResolver::new(pkg_resolver);
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
