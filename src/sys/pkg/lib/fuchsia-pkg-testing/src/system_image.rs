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
#[derive(Default)]
pub struct SystemImageBuilder<'a> {
    static_packages: Option<&'a [&'a Package]>,
    cache_packages: Option<&'a [&'a Package]>,
    pkgfs_non_static_packages_allowlist: Option<&'a [&'a str]>,
    pkgfs_disable_executability_restrictions: bool,
}

impl<'a> SystemImageBuilder<'a> {
    /// Returns an empty `SystemImageBuilder` configured with no static or cache package.
    pub fn new() -> Self {
        Self::default()
    }

    /// Use the supplied static_packages with the system image. Call at most once.
    pub fn static_packages(mut self, static_packages: &'a [&'a Package]) -> Self {
        assert!(self.static_packages.is_none());
        self.static_packages = Some(static_packages);
        self
    }

    /// Use the supplied cache_packages with the system image. Call at most once.
    pub fn cache_packages(mut self, cache_packages: &'a [&'a Package]) -> Self {
        assert!(self.cache_packages.is_none());
        self.cache_packages = Some(cache_packages);
        self
    }

    /// Use the supplied allowlist for the data file for pkgfs's /pkgfs/packages non-static
    /// packages allowlist. Call at most once.
    pub fn pkgfs_non_static_packages_allowlist(mut self, allowlist: &'a [&'a str]) -> Self {
        assert_eq!(self.pkgfs_non_static_packages_allowlist, None);
        self.pkgfs_non_static_packages_allowlist = Some(allowlist);
        self
    }

    /// Disable enforcement of executability restrictions for packages that are not base or
    /// allowlisted.
    pub fn pkgfs_disable_executability_restrictions(mut self) -> Self {
        self.pkgfs_disable_executability_restrictions = true;
        self
    }

    /// Build the system_image package.
    pub fn build(&self) -> impl Future<Output = Package> {
        fn packages_to_entries(pkgs: &[&Package]) -> Vec<(PackagePath, Hash)> {
            pkgs.iter()
                .map(|pkg| {
                    (
                        PackagePath::from_name_and_variant(pkg.name(), "0").unwrap(),
                        *pkg.meta_far_merkle_root(),
                    )
                })
                .collect()
        }

        let mut builder = PackageBuilder::new("system_image");
        let mut bytes = vec![];
        StaticPackages::from_entries(packages_to_entries(self.static_packages.unwrap_or(&[])))
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
        if let Some(allowlist) = &self.pkgfs_non_static_packages_allowlist {
            let contents = allowlist.join("\n");
            builder = builder.add_resource_at(
                "data/pkgfs_packages_non_static_packages_allowlist.txt",
                contents.as_bytes(),
            );
        }
        if self.pkgfs_disable_executability_restrictions {
            builder = builder
                .add_resource_at("data/pkgfs_disable_executability_restrictions", &[] as &[u8]);
        }
        async move { builder.build().await.unwrap() }
    }
}
