// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for SDK metadata.

use {
    anyhow::{bail, Context, Result},
    std::path::PathBuf,
};

/// Determine the path to the local metadata.
pub async fn local_metadata_dir(version: &str) -> Result<PathBuf> {
    let mut metadata_dir: PathBuf =
        ffx_config::get("pbms.storage.path").await.context("get pbms.storage.path")?;
    if version.contains("/") {
        // The version is used in a path. A leading slash will start the path at
        // root and nested slashes could allow "../".
        bail!("Slash in \"{}\". The sdk version must not contain a slash character.", version);
    }
    metadata_dir.push(version);
    Ok(metadata_dir)
}

/// Determine the path to the local images data.
///
/// The name "images" can be misleading, this also includes product related
/// data, such as .zbi files and so on.
pub(crate) async fn local_images_dir(version: &str, product_bundle_name: &str) -> Result<PathBuf> {
    let mut path = local_product_dir(version, product_bundle_name).await?;
    path.push("images");
    Ok(path)
}

/// Determine the path to the package data.
pub(crate) async fn local_packages_dir(
    version: &str,
    product_bundle_name: &str,
) -> Result<PathBuf> {
    let mut path = local_product_dir(version, product_bundle_name).await?;
    path.push("packages");
    Ok(path)
}

/// Determine the path to the product data.
async fn local_product_dir(version: &str, product_bundle_name: &str) -> Result<PathBuf> {
    let mut path = local_metadata_dir(version).await?;
    if product_bundle_name.contains("/") {
        // The product_bundle_name is used in a path. A leading slash will start
        // the path at root and nested slashes could allow "../".
        bail!(
            "Slash in {:?}. The product bundle name must not contain a slash character.",
            product_bundle_name
        );
    }
    path.push(product_bundle_name);
    Ok(path)
}

#[cfg(test)]
mod tests {
    use {super::*, ffx_config::ConfigLevel, tempfile::TempDir};

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic(expected = "The sdk version must not contain a slash character.")]
    async fn test_local_metadata_dir_panic() {
        local_metadata_dir("/slashed/version").await.expect("get path");
    }

    // Disabling this test until a test config can be modified without altering
    // the local user's config.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    #[serial_test::serial]
    async fn test_local_metadata_dir() {
        ffx_config::init(&[], None, None).expect("create test config");
        let temp_dir = TempDir::new().expect("temp dir");
        let path_str = temp_dir.path().to_str().expect("path to str");
        ffx_config::set(("pbms.storage.path", ConfigLevel::User), path_str.into())
            .await
            .expect("set path");
        let path = local_metadata_dir("fake_version").await.expect("get path");
        assert!(path.starts_with(&path_str));
        assert!(!path.is_dir());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic(expected = "The product bundle name must not contain a slash character.")]
    async fn test_local_images_dir_panic() {
        local_images_dir("fake_version", "/slashed/product/name").await.expect("get path");
    }

    // Disabling this test until a test config can be modified without altering
    // the local user's config.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    #[serial_test::serial]
    async fn test_local_images_dir() {
        ffx_config::init(&[], None, None).expect("create test config");
        let temp_dir = TempDir::new().expect("temp dir");
        let path_str = temp_dir.path().to_str().expect("path to str");
        ffx_config::set(("pbms.storage.path", ConfigLevel::User), path_str.into())
            .await
            .expect("set path");
        let path = local_images_dir("fake_version", "fake_product").await.expect("get path");
        assert!(path.starts_with(&path_str));
        assert!(!path.is_dir());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic(expected = "The product bundle name must not contain a slash character.")]
    async fn test_local_packages_dir_panic() {
        local_packages_dir("fake_version", "/slashed/product/name").await.expect("get path");
    }

    // Disabling this test until a test config can be modified without altering
    // the local user's config.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    #[serial_test::serial]
    async fn test_local_packages_dir() {
        ffx_config::init(&[], None, None).expect("create test config");
        let temp_dir = TempDir::new().expect("temp dir");
        let path_str = temp_dir.path().to_str().expect("path to str");
        ffx_config::set(("pbms.storage.path", ConfigLevel::User), path_str.into())
            .await
            .expect("set path");
        let path = local_packages_dir("fake_version", "fake_product").await.expect("get path");
        assert!(path.starts_with(&path_str));
        assert!(!path.is_dir());
    }
}
