// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for gcs metadata.

use {
    anyhow::{anyhow, bail, Context, Result},
    errors::ffx_bail,
    gcs::{
        client::{Client, ClientFactory, ProgressResult, ProgressState},
        error::GcsError,
        gs_url::split_gs_url,
        oauth2::new_refresh_token,
        token_store::{read_boto_refresh_token, write_boto_refresh_token, TokenStore},
    },
    std::path::{Path, PathBuf},
    structured_ui,
};

/// Create a GCS client that only allows access to public buckets.
pub(crate) fn get_gcs_client_without_auth() -> Client {
    let no_auth = TokenStore::new_without_auth();
    let client_factory = ClientFactory::new(no_auth);
    client_factory.create_client()
}

/// Returns the path to the .boto (gsutil) configuration file.
pub(crate) async fn get_boto_path<I>(ui: &I) -> Result<PathBuf>
where
    I: structured_ui::Interface + Sync,
{
    // TODO(fxb/89584): Change to using ffx client Id and consent screen.
    let boto: Option<PathBuf> =
        ffx_config::get("flash.gcs.token").await.context("getting flash.gcs.token config value")?;
    let boto_path = match boto {
        Some(boto_path) => boto_path,
        None => ffx_bail!(
            "GCS authentication configuration value \"flash.gcs.token\" not \
            found. Set this value by running `ffx config set flash.gcs.token <path>` \
            to the path of the .boto file."
        ),
    };
    if !boto_path.is_file() {
        update_refresh_token(&boto_path, ui).await.context("Set up refresh token")?
    }

    Ok(boto_path)
}

/// Returns a GCS client that can access public and private buckets.
///
/// `boto_path` is the path to the .boto (gsutil) configuration file.
pub(crate) fn get_gcs_client_with_auth(boto_path: &Path) -> Result<Client> {
    tracing::debug!("get_gcs_client_with_auth");
    let auth = TokenStore::new_with_auth(
        read_boto_refresh_token(boto_path)
            .context("read boto refresh")?
            .ok_or(anyhow!("Could not read boto token store"))?,
        /*access_token=*/ None,
    )?;

    let client_factory = ClientFactory::new(auth);
    Ok(client_factory.create_client())
}

/// Return true if the blob is available.
///
/// `gcs_url` is the full GCS url, e.g. "gs://bucket/path/to/file".
/// The resulting data will be written to a directory at `local_dir`.
pub(crate) async fn exists_in_gcs<I>(gcs_url: &str, ui: &I) -> Result<bool>
where
    I: structured_ui::Interface + Sync,
{
    let client = get_gcs_client_without_auth();
    let (bucket, gcs_path) = split_gs_url(gcs_url).context("Splitting gs URL.")?;
    match client.exists(bucket, gcs_path).await {
        Ok(exists) => Ok(exists),
        Err(_) => exists_in_gcs_with_auth(bucket, gcs_path, ui).await.context("fetch with auth"),
    }
}

/// Return true if the blob is available, using auth.
///
/// Fallback from using `exists_in_gcs()` without auth.
async fn exists_in_gcs_with_auth<I>(gcs_bucket: &str, gcs_path: &str, ui: &I) -> Result<bool>
where
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("exists_in_gcs_with_auth");
    let boto_path = get_boto_path(ui).await?;

    loop {
        let client = get_gcs_client_with_auth(&boto_path)?;
        match client.exists(gcs_bucket, gcs_path).await {
            Ok(exists) => return Ok(exists),
            Err(e) => match e.downcast_ref::<GcsError>() {
                Some(GcsError::NeedNewRefreshToken) => {
                    update_refresh_token(&boto_path, ui).await.context("Updating refresh token")?
                }
                Some(GcsError::NotFound(_, _)) => {
                    // Ok(false) should be returned rather than NotFound.
                    unreachable!();
                }
                Some(_) | None => bail!(
                    "Cannot get product bundle container while \
                    downloading from gs://{}/{}, error {:?}",
                    gcs_bucket,
                    gcs_path,
                    e,
                ),
            },
        }
    }
}

/// Download from a given `gcs_url`.
///
/// `gcs_url` is the full GCS url, e.g. "gs://bucket/path/to/file".
/// The resulting data will be written to a directory at `local_dir`.
pub(crate) async fn fetch_from_gcs<F, I>(
    gcs_url: &str,
    local_dir: &Path,
    progress: &F,
    ui: &I,
) -> Result<()>
where
    F: Fn(ProgressState<'_>, ProgressState<'_>) -> ProgressResult,
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("fetch_from_gcs {:?}", gcs_url);
    let client = get_gcs_client_without_auth();
    let (bucket, gcs_path) = split_gs_url(gcs_url).context("Splitting gs URL.")?;
    if !client.fetch_all(bucket, gcs_path, &local_dir, progress).await.is_ok() {
        tracing::debug!("Failed without auth, trying auth {:?}", gcs_url);
        fetch_from_gcs_with_auth(bucket, gcs_path, local_dir, progress, ui)
            .await
            .context("fetch with auth")?;
    }
    Ok(())
}

/// Download from a given `gcs_url` using auth.
///
/// Fallback from using `fetch_from_gcs()` without auth.
async fn fetch_from_gcs_with_auth<F, I>(
    gcs_bucket: &str,
    gcs_path: &str,
    local_dir: &Path,
    progress: &F,
    ui: &I,
) -> Result<()>
where
    F: Fn(ProgressState<'_>, ProgressState<'_>) -> ProgressResult,
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("fetch_from_gcs_with_auth");
    let boto_path = get_boto_path(ui).await?;

    loop {
        let client = get_gcs_client_with_auth(&boto_path)?;
        tracing::debug!("gcs_bucket {:?}, gcs_path {:?}", gcs_bucket, gcs_path);
        match client
            .fetch_all(gcs_bucket, gcs_path, &local_dir, progress)
            .await
            .context("fetch all")
        {
            Ok(()) => break,
            Err(e) => match e.downcast_ref::<GcsError>() {
                Some(GcsError::NeedNewRefreshToken) => {
                    update_refresh_token(&boto_path, ui).await.context("Updating refresh token")?
                }
                Some(GcsError::NotFound(b, p)) => {
                    tracing::warn!("[gs://{}/{} not found]", b, p);
                    break;
                }
                Some(_) | None => bail!(
                    "Cannot get product bundle container while \
                    downloading from gs://{}/{}, saving to {:?}, error {:?}",
                    gcs_bucket,
                    gcs_path,
                    local_dir,
                    e,
                ),
            },
        }
    }
    Ok(())
}

/// Prompt the user to visit the OAUTH2 permissions web page and enter a new
/// authorization code, then convert that to a refresh token and write that
/// refresh token to the ~/.boto file.
async fn update_refresh_token<I>(boto_path: &Path, _ui: &I) -> Result<()>
where
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("update_refresh_token {:?}", boto_path);
    println!("\nThe refresh token in the {:?} file needs to be updated.", boto_path);
    let refresh_token = new_refresh_token().await.context("get refresh token")?;
    write_boto_refresh_token(boto_path, &refresh_token)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, tempfile::NamedTempFile};

    // TODO(fxbug.dev/92773): This test requires mocks for interactivity and
    // https. The test is currently disabled.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_update_refresh_token() {
        let temp_file = NamedTempFile::new().expect("temp file");
        let ui = structured_ui::MockUi::new();
        update_refresh_token(&temp_file.path(), &ui).await.expect("set refresh token");
    }
}
