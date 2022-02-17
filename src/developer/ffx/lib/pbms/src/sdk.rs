// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for SDK metadata.

use {
    anyhow::{anyhow, bail, Context, Result},
    errors::ffx_bail,
    fms::Entries,
    futures_lite::stream::StreamExt,
    gcs::{
        client::ClientFactory,
        gs_url::split_gs_url,
        token_store::{
            auth_code_to_refresh, get_auth_code, read_boto_refresh_token, write_boto_refresh_token,
            GcsError, TokenStore,
        },
    },
    std::{
        fs::File,
        io::BufReader,
        path::{Path, PathBuf},
    },
};

/// Load FMS Entries for a given SDK `version`.
///
/// Tip: See `get_pbms()` for example usage.
pub(crate) async fn fetch_fms_entries_from_sdk(version: &str) -> Result<()> {
    let repos: Vec<String> =
        ffx_config::get("pbms.repos").await.context("get product-bundle.repos")?;
    for gcs_url in repos {
        let gcs_url = gcs_url.replace("{version}", version);
        let local_dir = local_metadata_dir(version).await.context("local metadata path")?;
        let temp_dir = tempfile::tempdir_in(&local_dir).context("create temp dir")?;
        fetch_from_gcs(&gcs_url, &temp_dir.path()).await.context("fetch from gcs")?;
        let hash = pb_file_name(&gcs_url);
        async_fs::rename(&temp_dir.path().join("product_bundles.json"), &local_dir.join(hash))
            .await
            .context("move temp file")?;
    }
    Ok(())
}

/// Generate a (likely) unique name for the URL.
///
/// Multiple repos may have identical names for their product bundles.
/// Instead of using the given file name a hash of the URL is used.
fn pb_file_name(gcs_url: &str) -> String {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::Hash;
    use std::hash::Hasher;
    let mut s = DefaultHasher::new();
    gcs_url.hash(&mut s);
    s.finish().to_string()
}

/// Download from a given `gcs_url`.
///
/// `gcs_url` is the full GCS url, e.g. "gs://bucket/path/to/file".
/// The resulting data will be written to a directory at `local_dir`.
pub(crate) async fn fetch_from_gcs(gcs_url: &str, local_dir: &Path) -> Result<()> {
    // TODO(fxb/89584): Change to using ffx client Id and consent screen.
    let boto: Option<PathBuf> = ffx_config::file("flash.gcs.token")
        .await
        .context("getting flash.gcs.token config value")?;
    let boto_path = match boto {
        Some(boto_path) => boto_path,
        None => ffx_bail!(
            "GCS authentication configuration value \"flash.gcs.token\" not \
            found. Set this value by running `ffx config set flash.gcs.token <path>` \
            to the path of the .boto file."
        ),
    };
    loop {
        let auth = TokenStore::new_with_auth(
            read_boto_refresh_token(&boto_path)?
                .ok_or(anyhow!("Could not read boto token store"))?,
            /*access_token=*/ None,
        )?;

        let client_factory = ClientFactory::new(auth);
        let client = client_factory.create_client();
        let url_string = gcs_url.to_string();
        let (bucket, gcs_path) = split_gs_url(&url_string).context("Splitting gs URL.")?;
        match client.fetch_all(bucket, gcs_path, &local_dir).await.context("fetch all") {
            Ok(()) => break,
            Err(e) => match e.downcast_ref::<GcsError>() {
                Some(GcsError::NeedNewRefreshToken) => {
                    update_refresh_token(&boto_path).await.context("Updating refresh token")?
                }
                Some(_) | None => bail!(
                    "Cannot get product bundle container while \
                     downloading from {:?}, saving to {:?}, error {:?}",
                    gcs_url,
                    local_dir,
                    e,
                ),
            },
        }
    }
    Ok(())
}

/// Load FMS Entries from the local metadata.
///
/// Whether the sdk or local build is used is determined by ffx_config::sdk.
/// No attempt is made to refresh the metadata (no network IO).
pub(crate) async fn local_entries(version: &str) -> Result<Entries> {
    let pb_path = local_metadata_dir(version).await.context("local metadata path")?;
    let mut entries = Entries::new();
    let mut dir = async_fs::read_dir(&pb_path).await?;
    while let Some(entry) = dir.try_next().await? {
        let path = entry.path();
        if !path.is_file() {
            continue;
        }
        let mut container = File::open(&path)
            .map(BufReader::new)
            .with_context(|| format!("Open file \"{}\".", path.display()))?;
        entries.add_json(&mut container)?;
    }
    Ok(entries)
}

/// Prompt the user to visit the OAUTH2 permissions web page and enter a new
/// authorization code, then convert that to a refresh token and write that
/// refresh token to the ~/.boto file.
async fn update_refresh_token(boto_path: &Path) -> Result<()> {
    println!("\nThe refresh token in the {:?} file needs to be updated.", boto_path);
    let auth_code = get_auth_code()?;
    let refresh_token = auth_code_to_refresh(&auth_code).await.context("get refresh token")?;
    write_boto_refresh_token(boto_path, &refresh_token)?;
    Ok(())
}

/// Determine the path to the local metadata.
async fn local_metadata_dir(version: &str) -> Result<PathBuf> {
    let mut metadata_dir: PathBuf =
        ffx_config::get("pbms.data.path").await.context("get product-bundle.data.path")?;
    if version.contains("/") {
        // The version is used in a path. A leading slash will start the path at
        // root and nested slashes could allow "../".
        bail!("Slash in \"{}\". The sdk version must not contain a slash character.", version);
    }
    metadata_dir.push(version);
    std::fs::create_dir_all(&metadata_dir)?;
    Ok(metadata_dir)
}

/// Determine the path to the local images data.
///
/// The name "images" can be misleading, this also includes product related
/// data, such as .zbi files and so on.
pub(crate) async fn local_images_dir(version: &str, product_bundle_name: &str) -> Result<PathBuf> {
    let mut path = local_product_dir(version, product_bundle_name).await?;
    path.push("images");
    async_fs::create_dir_all(&path).await?;
    Ok(path)
}

/// Determine the path to the package data.
pub(crate) async fn local_packages_dir(
    version: &str,
    product_bundle_name: &str,
) -> Result<PathBuf> {
    let mut path = local_product_dir(version, product_bundle_name).await?;
    path.push("packages");
    async_fs::create_dir_all(&path).await?;
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
    use {
        super::*,
        ffx_config::ConfigLevel,
        tempfile::{NamedTempFile, TempDir},
    };

    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_pbm() {
        let temp_file = NamedTempFile::new().expect("temp file");
        update_refresh_token(&temp_file.path()).await.expect("set refresh token");
    }

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
        ffx_config::set(("pbms.data.path", ConfigLevel::User), path_str.into())
            .await
            .expect("set path");
        let path = local_metadata_dir("fake_version").await.expect("get path");
        assert!(path.starts_with(&path_str));
        assert!(path.is_dir());
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
        ffx_config::set(("pbms.data.path", ConfigLevel::User), path_str.into())
            .await
            .expect("set path");
        let path = local_images_dir("fake_version", "fake_product").await.expect("get path");
        assert!(path.starts_with(&path_str));
        assert!(path.is_dir());
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
        ffx_config::set(("pbms.data.path", ConfigLevel::User), path_str.into())
            .await
            .expect("set path");
        let path = local_packages_dir("fake_version", "fake_product").await.expect("get path");
        assert!(path.starts_with(&path_str));
        assert!(path.is_dir());
    }
}
