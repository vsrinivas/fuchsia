// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{Package, PackageBuilder},
    fuchsia_merkle::Hash,
    fuchsia_pkg::PackagePath,
    std::future::Future,
    system_image::{CachePackages, StaticPackages},
};

/// Builds a system_image package.
pub struct SystemImageBuilder<'a> {
    static_packages: &'a [&'a Package],
    cache_packages: Option<&'a [&'a Package]>,
}

impl<'a> SystemImageBuilder<'a> {
    /// Create an instance of the builder from the (possibly empty) list of base packages.
    pub fn new(static_packages: &'a [&'a Package]) -> Self {
        Self { static_packages, cache_packages: None }
    }

    /// Use the supplied cache_packages with the system image. Call at most once.
    pub fn cache_packages(self, cache_packages: &'a [&'a Package]) -> Self {
        assert!(self.cache_packages.is_none());
        Self { static_packages: self.static_packages, cache_packages: Some(cache_packages) }
    }

    /// Build the system_image package.
    pub fn build(&self) -> impl Future<Output = Package> {
        fn packages_to_entries(pkgs: &[&Package]) -> Vec<(PackagePath, Hash)> {
            pkgs.iter()
                .map(|pkg| {
                    (
                        PackagePath::from_name_and_variant(pkg.name(), "0").unwrap(),
                        pkg.meta_far_merkle_root().clone(),
                    )
                })
                .collect()
        }

        let mut builder = PackageBuilder::new("system_image");
        let mut bytes = vec![];
        StaticPackages::from_entries(packages_to_entries(self.static_packages))
            .serialize(&mut bytes)
            .unwrap();
        builder = builder.add_resource_at("data/static_packages", bytes.as_slice());
        if let Some(cache_packages) = self.cache_packages {
            bytes.clear();
            CachePackages::from_entries(packages_to_entries(cache_packages))
                .serialize(&mut bytes)
                .unwrap();
            builder = builder.add_resource_at("data/cache_packages", bytes.as_slice());
        }
        async move { builder.build().await.unwrap() }
    }
}
