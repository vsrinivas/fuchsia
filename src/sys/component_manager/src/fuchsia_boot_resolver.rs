// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::resolver::{Resolver, ResolverError, ResolverFut},
    anyhow::Error,
    cm_fidl_translator::translate,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::boot_url::BootUrl,
    std::path::Path,
};

pub static SCHEME: &str = "fuchsia-boot";

/// Resolves component URLs with the "fuchsia-boot" scheme, which supports loading components from
/// the /boot directory in component_manager's namespace.
///
/// On a typical system, this /boot directory is the bootfs served from the contents of the
/// 'ZBI_TYPE_STORAGE_BOOTFS' ZBI item by bootsvc, the process which starts component_manager.
///
/// URL syntax:
/// - fuchsia-boot:///path/within/bootfs#meta/component.cm
pub struct FuchsiaBootResolver {
    boot_proxy: DirectoryProxy,
}

impl FuchsiaBootResolver {
    /// Create a new FuchsiaBootResolver. This first checks whether a /boot directory is present in
    /// the namespace, and returns Ok(None) if not present. This is generally the case in unit and
    /// integration tests where this resolver is unused.
    pub fn new() -> Result<Option<FuchsiaBootResolver>, Error> {
        // Note that this check is synchronous. The async executor also likely is not being polled
        // yet, since this is called during startup.
        let bootfs_dir = Path::new("/boot");
        if !bootfs_dir.exists() {
            return Ok(None);
        }

        let proxy = io_util::open_directory_in_namespace(
            bootfs_dir.to_str().unwrap(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        Ok(Some(Self::new_from_directory(proxy)))
    }

    /// Create a new FuchsiaBootResolver that resolves URLs within the given directory. Used for
    /// injection in unit tests.
    fn new_from_directory(proxy: DirectoryProxy) -> FuchsiaBootResolver {
        FuchsiaBootResolver { boot_proxy: proxy }
    }

    async fn resolve_async<'a>(
        &'a self,
        component_url: &'a str,
    ) -> Result<fsys::Component, ResolverError> {
        // Parse URL.
        let url = BootUrl::parse(component_url)
            .map_err(|e| ResolverError::component_not_available(component_url, e))?;
        // Package path is 'canonicalized' to ensure that it is relative, since absolute paths will
        // be (inconsistently) rejected by fuchsia.io methods.
        let package_path = Path::new(io_util::canonicalize_path(url.path()));
        let res = url.resource().ok_or(ResolverError::url_missing_resource_error(component_url))?;
        let res_path = package_path.join(res);

        // Read component manifest from resource into a component decl.
        let cm_file = io_util::open_file(&self.boot_proxy, &res_path, fio::OPEN_RIGHT_READABLE)
            .map_err(|e| ResolverError::manifest_not_available(component_url, e))?;
        let cm_str = io_util::read_file(&cm_file)
            .await
            .map_err(|e| ResolverError::manifest_not_available(component_url, e))?;
        let component_decl =
            translate(&cm_str).map_err(|e| ResolverError::manifest_invalid(component_url, e))?;

        // Set up the fuchsia-boot path as the component's "package" namespace.
        let path_proxy = io_util::open_directory(
            &self.boot_proxy,
            package_path,
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )
        .map_err(|e| {
            ResolverError::component_not_available(
                component_url,
                e.context("failed to open package directory"),
            )
        })?;
        let package = fsys::Package {
            package_url: Some(url.root_url().to_string()),
            package_dir: Some(ClientEnd::new(path_proxy.into_channel().unwrap().into_zx_channel())),
        };

        Ok(fsys::Component {
            resolved_url: Some(component_url.to_string()),
            decl: Some(component_decl),
            package: Some(package),
        })
    }
}

impl Resolver for FuchsiaBootResolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut {
        Box::pin(self.resolve_async(component_url))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy_and_stream, ServerEnd},
        fidl_fuchsia_data as fdata,
        fidl_fuchsia_io::{DirectoryMarker, DirectoryRequest, NodeMarker},
        fidl_fuchsia_sys2::ComponentDecl,
        fuchsia_async as fasync,
        futures::prelude::*,
        std::path::PathBuf,
    };

    // Simulate a fake bootfs Directory service that only contains a single directory
    // ("packages/hello_world"), using our own package directory (hosted by the real pkgfs) as the
    // contents.
    // TODO(fxb/37534): This is implemented by manually handling the Directory.Open and forwarding
    // to the test's real package directory because Rust vfs does not yet support
    // OPEN_RIGHT_EXECUTABLE. Simplify in the future.
    // TODO: Switch this test to use a hardcoded manifest string & consider removing this test
    // manifest from the test package completely (after cleaning up other test dependencies).
    struct FakeBootfs;

    impl FakeBootfs {
        pub fn new() -> DirectoryProxy {
            let (proxy, mut stream) = create_proxy_and_stream::<DirectoryMarker>().unwrap();
            fasync::spawn_local(async move {
                while let Some(request) = stream.try_next().await.unwrap() {
                    match request {
                        DirectoryRequest::Open {
                            flags,
                            mode: _,
                            path,
                            object,
                            control_handle: _,
                        } => Self::handle_open(&path, flags, object),
                        _ => panic!("Fake doesn't support request: {:?}", request),
                    }
                }
            });
            proxy
        }

        fn handle_open(path: &str, flags: u32, server_end: ServerEnd<NodeMarker>) {
            if path.is_empty() {
                // We don't support this in this fake, drop the server_end
                return;
            }
            let path = Path::new(path);
            let mut path_iter = path.iter();

            // The test URLs used below have "packages/" as the first path component
            assert_eq!("packages", path_iter.next().unwrap().to_str().unwrap());

            // We're simulating a package server that only contains the "hello_world"
            // package. This returns rather than asserts so that we can attempt to resolve
            // other packages.
            if path_iter.next().unwrap().to_str().unwrap() != "hello_world" {
                return;
            }

            // Connect the server_end by forwarding to our real package directory, which can handle
            // OPEN_RIGHT_EXECUTABLE. Also, pass through the input flags here to ensure that we
            // don't artificially pass the test (i.e. the resolver needs to ask for the appropriate
            // rights).
            let mut open_path = PathBuf::from("/pkg");
            open_path.extend(path_iter);
            io_util::connect_in_namespace(
                open_path.to_str().unwrap(),
                server_end.into_channel(),
                flags,
            )
            .expect("failed to open path in namespace");
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn hello_world_test() -> Result<(), Error> {
        let resolver = FuchsiaBootResolver::new_from_directory(FakeBootfs::new());

        let url =
            "fuchsia-boot:///packages/hello_world#meta/component_manager_tests_hello_world.cm";
        let component = resolver.resolve_async(url).await?;

        // Check that both the returned component manifest and the component manifest in
        // the returned package dir match the expected value. This also tests that
        // the resolver returned the right package dir.
        let fsys::Component { resolved_url, decl, package } = component;
        assert_eq!(url, resolved_url.unwrap());
        let program = fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: "binary".to_string(),
                value: Some(Box::new(fdata::DictionaryValue::Str("bin/hello_world".to_string()))),
            }]),
        };
        let expected_decl = ComponentDecl {
            program: Some(program),
            uses: Some(vec![fsys::UseDecl::Runner(fsys::UseRunnerDecl {
                source_name: Some("elf".to_string()),
            })]),
            exposes: None,
            offers: None,
            facets: None,
            children: None,
            collections: None,
            storage: None,
            runners: None,
            environments: None,
            resolvers: None,
        };
        assert_eq!(decl.unwrap(), expected_decl);

        let fsys::Package { package_url, package_dir } = package.unwrap();
        assert_eq!(package_url.unwrap(), "fuchsia-boot:///packages/hello_world");

        let dir_proxy = package_dir.unwrap().into_proxy().unwrap();
        let path = Path::new("meta/component_manager_tests_hello_world.cm");
        let file_proxy = io_util::open_file(&dir_proxy, path, fio::OPEN_RIGHT_READABLE)
            .expect("could not open cm");
        let cm_contents = io_util::read_file(&file_proxy).await.expect("could not read cm");
        assert_eq!(
            cm_fidl_translator::translate(&cm_contents).expect("could not parse cm"),
            expected_decl
        );

        // Try to load an executable file, like a binary, reusing the library_loader helper that
        // opens with OPEN_RIGHT_EXECUTABLE and gets a VMO with VMO_FLAG_EXEC.
        library_loader::load_vmo(&dir_proxy, "bin/hello_world")
            .await
            .expect("failed to open executable file");

        Ok(())
    }
}
