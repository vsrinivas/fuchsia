// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::resolver::{Resolver, ResolverError, ResolverFut},
    cm_fidl_translator::translate,
    failure::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryProxy,
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
            io_util::OPEN_RIGHT_READABLE,
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
        let cm_file = io_util::open_file(&self.boot_proxy, &res_path, io_util::OPEN_RIGHT_READABLE)
            .map_err(|e| ResolverError::component_not_available(component_url, e))?;
        let cm_str = io_util::read_file(&cm_file)
            .await
            .map_err(|e| ResolverError::component_not_available(component_url, e))?;
        let component_decl = translate(&cm_str)
            .map_err(|e| ResolverError::component_not_available(component_url, e))?;

        // Set up the fuchsia-boot path as the component's "package" namespace.
        let path_proxy =
            io_util::open_directory(&self.boot_proxy, package_path, io_util::OPEN_RIGHT_READABLE)
                .map_err(|e| ResolverError::component_not_available(component_url, e))?;
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
        fidl::endpoints,
        fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
        fidl_fuchsia_sys2::ComponentDecl,
        fuchsia_async::{
            EHandle,
            self as fasync,
        },
        fuchsia_vfs_pseudo_fs_mt::{
            directory::entry::DirectoryEntry,
            execution_scope::ExecutionScope,
            file::pcb::asynchronous::read_only_static,
            path,
            pseudo_directory,
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn hello_world_test() -> Result<(), Error> {
        // We inject a fake /boot for this test. We could alternatively install the dir as /boot in
        // the test's namespace, but that modifies global process state and thus might interact
        // poorly with other tests.
        // TODO: Switch this test to use a hardcoded manifest string & consider removing this test
        // manifest from the test package completely (after cleaning up other test dependencies).
        let cm_path = Path::new("/pkg/meta/component_manager_tests_hello_world.cm");
        let cm_bytes = std::fs::read(cm_path)?;
        let (proxy, server_end) = endpoints::create_proxy::<fio::NodeMarker>()?;
        let dir = pseudo_directory! {
            "packages" => pseudo_directory! {
                "hello_world" => pseudo_directory! {
                    "meta" => pseudo_directory! {
                        "component_manager_tests_hello_world.cm" => read_only_static(cm_bytes.clone()),
                    },
                },
            },
        };
        dir.open(
            ExecutionScope::from_executor(Box::new(EHandle::local())),
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            path::Path::empty(),
            server_end,
        );
        let proxy = io_util::node_to_directory(proxy)?;
        let resolver = FuchsiaBootResolver::new_from_directory(proxy);

        let url =
            "fuchsia-boot:///packages/hello_world#meta/component_manager_tests_hello_world.cm";
        let component = resolver.resolve_async(url).await?;
        assert_eq!(url, component.resolved_url.unwrap());

        let program = fdata::Dictionary {
            entries: vec![fdata::Entry {
                key: "binary".to_string(),
                value: Some(Box::new(fdata::Value::Str("bin/hello_world".to_string()))),
            }],
        };
        let component_decl = ComponentDecl {
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
        assert_eq!(component_decl, component.decl.unwrap());
        assert_eq!(
            "fuchsia-boot:///packages/hello_world",
            component.package.unwrap().package_url.unwrap()
        );
        Ok(())
    }
}
