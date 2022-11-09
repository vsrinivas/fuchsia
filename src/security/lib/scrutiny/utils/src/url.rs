// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Result},
    fuchsia_hash::Hash,
    fuchsia_url::{AbsolutePackageUrl, PackageName, PackageVariant, RepositoryUrl},
    std::fmt::Display,
    tracing::warn,
};

// TODO(fxbug.dev/101234): Eliminate this API by requiring clients to declare a repository.
pub fn from_package_name<D: Display>(name: D) -> Result<AbsolutePackageUrl> {
    warn!(package_name = %name, "Assuming package is served from fuchsia-pkg://fuchsia.com");
    let url_string = format!("fuchsia-pkg://fuchsia.com/{}", name);
    let url = AbsolutePackageUrl::parse(&url_string)
        .with_context(|| format!("Error parsing URL {}", url_string))?;
    if url.variant().is_some() {
        bail!("Expected no variant constructing URL {} with package name {}", url_string, name);
    }
    if url.hash().is_some() {
        bail!("Expected no hash constructing URL {} with package name {}", url_string, name);
    }
    Ok(url)
}

// TODO(fxbug.dev/101234): Eliminate this API by requiring clients to declare a repository.
pub fn from_package_name_variant_path<D1: Display>(
    name_variant_path: D1,
) -> Result<AbsolutePackageUrl> {
    warn!(
        path = %name_variant_path,
        "Assuming package/variant is served from fuchsia-pkg://fuchsia.com",
    );
    let url_string = format!("fuchsia-pkg://fuchsia.com/{}", name_variant_path);
    let url = AbsolutePackageUrl::parse(&url_string).with_context(|| {
        format!("Error parsing URL {} with package name/variant path", url_string)
    })?;
    if url.hash().is_some() {
        bail!(
            "Expected no hash constructing URL {} with package name/variant path {}",
            url_string,
            name_variant_path
        );
    }
    Ok(url)
}

// TODO(fxbug.dev/101234): Eliminate this API by requiring clients to declare a repository.
pub fn from_package_name_and_hash<D1: Display, D2: Display>(
    name: D1,
    hash: D2,
) -> Result<AbsolutePackageUrl> {
    warn!(
        package_name = %name,
        %hash,
        "Assuming package is served from fuchsia-pkg://fuchsia.com",
    );
    let url_string = format!("fuchsia-pkg://fuchsia.com/{}?hash={}", name, hash);
    let url = AbsolutePackageUrl::parse(&url_string)
        .with_context(|| format!("Error parsing URL {} with package name and hash", url_string))?;
    if url.variant().is_some() {
        bail!(
            "Expected no variant constructing URL {} with package name {} and hash {}",
            url_string,
            name,
            hash
        );
    }
    if url.hash().is_none() {
        bail!(
            "Expected hash constructing URL {} with package name {} and hash {}",
            url_string,
            name,
            hash
        );
    }
    Ok(url)
}

pub fn from_pkg_url_parts(
    name: PackageName,
    variant: Option<PackageVariant>,
    hash: Option<Hash>,
) -> Result<AbsolutePackageUrl> {
    let repo_url = RepositoryUrl::parse_host("fuchsia.com".to_string())
        .context("Failed to construct fuchsia.com RepoUrl")?;
    Ok(AbsolutePackageUrl::new(repo_url, name, variant, hash))
}
