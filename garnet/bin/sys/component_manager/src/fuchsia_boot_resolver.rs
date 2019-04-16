// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io_util,
    crate::model::{Resolver, ResolverError},
    cm_fidl_translator::translate,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_uri::boot_uri::BootUri,
    futures::future::FutureObj,
    std::path::PathBuf,
};

pub static SCHEME: &str = "fuchsia-boot";

/// Resolves component URIs with the "fuchsia-boot" scheme.
///
/// URI syntax:
/// - fuchsia-boot:///directory#meta/component.cm
pub struct FuchsiaBootResolver {}

impl FuchsiaBootResolver {
    pub fn new() -> FuchsiaBootResolver {
        FuchsiaBootResolver {}
    }

    async fn resolve_async<'a>(
        &'a self,
        component_uri: &'a str,
    ) -> Result<fsys::Component, ResolverError> {
        // Parse URI.
        let uri = BootUri::parse(component_uri)
            .map_err(|e| ResolverError::component_not_available(component_uri, e))?;
        let res = uri.resource().ok_or(ResolverError::uri_missing_resource_error(component_uri))?;
        let res_path = PathBuf::from(uri.path()).join(PathBuf::from(res));
        let res_path_str =
            res_path.to_str().ok_or(ResolverError::uri_missing_resource_error(component_uri))?;

        // Read component manifest from resource into a component decl.
        let cm_file = io_util::open_file_in_namespace(&res_path_str)
            .map_err(|e| ResolverError::component_not_available(component_uri, e))?;
        let cm_str = await!(io_util::read_file(&cm_file))
            .map_err(|e| ResolverError::component_not_available(component_uri, e))?;
        let component_decl = translate(&cm_str)
            .map_err(|e| ResolverError::component_not_available(component_uri, e))?;

        // Set up the fuchsia-boot path as the component's "package" namespace.
        let package_path = uri.path();
        let path_proxy = io_util::open_directory_in_namespace(&package_path)
            .map_err(|e| ResolverError::component_not_available(component_uri, e))?;
        let package = fsys::Package {
            package_uri: Some(uri.root_uri().to_string()),
            package_dir: Some(ClientEnd::new(path_proxy.into_channel().unwrap().into_zx_channel())),
        };

        Ok(fsys::Component {
            resolved_uri: Some(component_uri.to_string()),
            decl: Some(component_decl),
            package: Some(package),
        })
    }
}

impl Resolver for FuchsiaBootResolver {
    fn resolve<'a>(
        &'a self,
        component_uri: &'a str,
    ) -> FutureObj<'a, Result<fsys::Component, ResolverError>> {
        FutureObj::new(Box::new(self.resolve_async(component_uri)))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_data as fdata, fidl_fuchsia_sys2::ComponentDecl,
        fuchsia_async as fasync,
    };

    #[test]
    fn hello_world_test() {
        let mut executor = fasync::Executor::new().unwrap();
        executor.run_singlethreaded(
            async {
                let resolver = FuchsiaBootResolver::new();
                let component = await!(resolver.resolve_async(
                    "fuchsia-boot:///pkg#meta/component_manager_tests_hello_world.cm"
                ))
                .unwrap();
                assert_eq!(
                    "fuchsia-boot:///pkg#meta/component_manager_tests_hello_world.cm",
                    component.resolved_uri.unwrap()
                );
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
                };
                assert_eq!(component_decl, component.decl.unwrap());
                assert_eq!("fuchsia-boot:///pkg", component.package.unwrap().package_uri.unwrap());
            },
        );
    }
}
