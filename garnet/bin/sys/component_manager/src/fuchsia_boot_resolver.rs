// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{Resolver, ResolverError},
    fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
    futures::future::FutureObj,
};

pub static SCHEME: &str = "fuchsia-boot";

/// Resolves component URIs with the "fuchsia-boot" scheme.
///
/// URI syntax:
/// - fuchsia-boot:///path/to/directory#meta/component.cm
pub struct FuchsiaBootResolver {}

impl FuchsiaBootResolver {
    pub fn new() -> FuchsiaBootResolver {
        FuchsiaBootResolver {}
    }

    async fn resolve_async<'a>(
        &'a self, component_uri: &'a str,
    ) -> Result<fsys::Component, ResolverError> {
        // TODO: Actually resolve and parse CM files from the boot filesystem.
        println!(
            "FuchsiaBootResolver: pretending to resolve '{}'",
            component_uri
        );
        if component_uri == "fuchsia-boot:///boot#meta/scaffold.cm" {
            Ok(fsys::Component {
                resolved_uri: Some(component_uri.to_string()),
                decl: Some(fsys::ComponentDecl {
                    program: Some(fdata::Dictionary {
                        entries: vec![fdata::Entry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::Value::Str("bin/scaffold".to_string()))),
                        }],
                    }),
                    uses: None,
                    exposes: None,
                    offers: None,
                    facets: None,
                    children: None,
                }),
                package: None,
            })
        } else {
            Err(ResolverError::ComponentNotAvailable)
        }
    }
}

impl Resolver for FuchsiaBootResolver {
    fn resolve<'a>(
        &'a self, component_uri: &'a str,
    ) -> FutureObj<'a, Result<fsys::Component, ResolverError>> {
        FutureObj::new(Box::new(self.resolve_async(component_uri)))
    }
}
