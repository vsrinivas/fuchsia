// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    fuchsia_pkg::{PackageName, PackagePath, PackageVariant},
    once_cell::sync::Lazy,
    url::Url,
};

static PKG_CACHE_PACKAGE_URL: Lazy<Url> =
    Lazy::new(|| "fuchsia-pkg-cache:///".parse().expect("valid url"));
static PKG_CACHE_COMPONENT_URL: Lazy<Url> =
    Lazy::new(|| "fuchsia-pkg-cache:///#meta/pkg-cache.cm".parse().expect("valid url"));
static PKG_CACHE_PATH: Lazy<PackagePath> =
    Lazy::new(|| "pkg-cache/0".parse().expect("valid package path literal"));
static PKG_CACHE_PACKAGE_NAME_AND_VARIANT: Lazy<(PackageName, PackageVariant)> =
    Lazy::new(|| PKG_CACHE_PATH.clone().into_name_and_variant());

/// The package URL for the `pkg-cache` package `fuchsia-pkg-cache` scheme.
pub fn fuchsia_pkg_cache_package_url() -> &'static Url {
    &PKG_CACHE_PACKAGE_URL
}

/// The component URL for the `pkg-cache` component under the `fuchsia-pkg-cache` scheme.
pub fn fuchsia_pkg_cache_component_url() -> &'static Url {
    &PKG_CACHE_COMPONENT_URL
}

/// The component manifest path for the `pkg-cache` component manifest under the `fuchsia-pkg-cache`
/// scheme.
pub fn fuchsia_pkg_cache_manifest_path_str() -> &'static str {
    PKG_CACHE_COMPONENT_URL.fragment().expect("url fragment")
}

/// The package path that designates `pkg-cache`, which may be referred to via
/// the `fuchsia-pkg` scheme in, for example, an update package, or via the `fuchsia-pkg-cache`
/// scheme in, for example a system bootstrap realm.
pub fn pkg_cache_package_path() -> &'static PackagePath {
    &PKG_CACHE_PATH
}

/// The package name and variant that designates `pkg-cache`, which may be referred to via
/// the `fuchsia-pkg` scheme in, for example, an update package, or via the `fuchsia-pkg-cache`
/// scheme in, for example a system bootstrap realm.
pub fn pkg_cache_package_name_and_variant() -> &'static (PackageName, PackageVariant) {
    &PKG_CACHE_PACKAGE_NAME_AND_VARIANT
}

#[cfg(test)]
mod tests {
    use super::{
        fuchsia_pkg_cache_component_url, fuchsia_pkg_cache_manifest_path_str,
        fuchsia_pkg_cache_package_url, pkg_cache_package_name_and_variant, pkg_cache_package_path,
    };

    // Ensure that accessors do not cause fatal errors initializing their data.
    #[fuchsia::test]
    fn fuchsia_pkg_cache_smoke_test() {
        let _ = (
            fuchsia_pkg_cache_package_url(),
            fuchsia_pkg_cache_component_url(),
            pkg_cache_package_path(),
            pkg_cache_package_name_and_variant(),
        );
        assert_eq!(fuchsia_pkg_cache_manifest_path_str(), "meta/pkg-cache.cm")
    }
}
