// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for SDK metadata.

use {
    anyhow::{anyhow, bail, Context, Result},
    errors::ffx_bail,
    fms::Entries,
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
pub(crate) async fn fetch_from_sdk(version: &str) -> Result<()> {
    // Part of the `gs:` URL used to download the metadata.
    let gcs_url = format!("gs://fuchsia-sdk/development/{}/sdk/product_bundles.json", version);
    let local_dir = local_metadata_dir().await.context("local metadata path")?;
    fetch_from_gcs(&gcs_url, &local_dir).await.context("fetch from gcs")
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
        match client.fetch_all(bucket, gcs_path, &local_dir).await {
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
pub(crate) async fn local_entries() -> Result<Entries> {
    let pb_path =
        local_metadata_dir().await.context("local metadata path")?.join("product_bundles.json");
    let mut container = File::open(&pb_path).map(BufReader::new).context("open file")?;
    let mut entries = Entries::new();
    entries.add_json(&mut container)?;
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
async fn local_metadata_dir() -> Result<PathBuf> {
    let config_path: PathBuf =
        ffx_config::get("pbms.data.path").await.context("get product-bundle.data.path")?;
    std::fs::create_dir_all(&config_path)?;
    Ok(config_path)
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
    async fn test_local_metadata_dir() {
        ffx_config::init(&[], None, None).expect("create test config");
        let temp_dir = TempDir::new().expect("temp dir");
        let path_str = temp_dir.path().to_str().expect("path to str");
        ffx_config::set(("pbms.data.path", ConfigLevel::User), path_str.into())
            .await
            .expect("set path");
        let path = local_metadata_dir().await.expect("get path");
        assert!(path.starts_with(&temp_dir));
        assert!(Path::new(path_str).is_dir());
    }
}
