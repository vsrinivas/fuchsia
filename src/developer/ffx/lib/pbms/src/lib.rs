// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Access utilities for SDK metadata.

use {
    anyhow::{anyhow, bail, Context, Result},
    errors::{ffx_bail, ffx_error},
    ffx_config::sdk::SdkVersion,
    fms::Entries,
    gcs::{
        client::ClientFactory,
        token_store::{
            auth_code_to_refresh, get_auth_code, read_boto_refresh_token, write_boto_refresh_token,
            GcsError, TokenStore,
        },
    },
    serde::Deserialize,
    std::{
        fs::File,
        io::BufReader,
        path::{Path, PathBuf},
    },
    tempfile::tempdir,
};

/// Load FMS Entries from the SDK or in-tree build.
///
/// Whether the sdk or local build is used is determined by ffx_config::sdk.
pub async fn read_pbms() -> Result<Entries> {
    let sdk = ffx_config::get_sdk().await.context("PBMS ffx config get sdk")?;
    match sdk.get_version() {
        SdkVersion::Version(v) => Ok(pbms_from_sdk(v).await?),
        SdkVersion::InTree => Ok(pbms_from_tree(sdk.get_path_prefix()).await?),
        SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
    }
}

/// Load FMS Entries for a given SDK `version`.
///
/// Tip: See `read_pbms()` for example usage.
pub async fn pbms_from_sdk(version: &str) -> Result<Entries> {
    let temp_dir = tempdir()?;
    let product_bundle_container_path = temp_dir.path().join("product_bundles.json");
    let gcs_path = format!("development/{}/sdk/product_bundles.json", version);

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
        match client.fetch("fuchsia-sdk", &gcs_path, &product_bundle_container_path).await {
            Ok(()) => break,
            Err(e) => match e.downcast_ref::<GcsError>() {
                Some(GcsError::NeedNewRefreshToken) => {
                    update_refresh_token(&boto_path).await.context("Updating refresh token")?
                }
                Some(_) | None => bail!("Cannot get product bundle container: {:?}", e),
            },
        }
    }

    let mut container = File::open(&product_bundle_container_path).map(BufReader::new)?;
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
    let refresh_token = auth_code_to_refresh(&auth_code).await?;
    write_boto_refresh_token(boto_path, &refresh_token)?;
    Ok(())
}

/// Outer type in a "images.json" file.
#[derive(Default, Deserialize)]
pub struct Images(Vec<Image>);

/// Individual records in an "images.json" file.
#[derive(Default, Deserialize)]
pub struct Image {
    pub name: String,
    pub path: String,
    // Ignore the rest of the fields.
}

/// Load FMS Entries from the local in-tree build (rather than from the SDK).
///
/// Tip: `let build_path = sdk.get_path_prefix();`.
pub async fn pbms_from_tree(build_path: &Path) -> Result<Entries> {
    // The product_bundle.json path will be a relative path under `build_path`.
    // The "images.json" file provides an index of named items to paths. Open
    // "images.json" and search for the path of the "product_bundle" entry.
    let manifest_path = build_path.join("images.json");
    let images: Images = File::open(manifest_path.clone())
        .map_err(|e| {
            ffx_error!(
                "Cannot open file {:?}\nerror: {:?}. Doing `fx build` may help.",
                manifest_path,
                e
            )
        })
        .map(BufReader::new)
        .map(serde_json::from_reader)?
        .map_err(|e| anyhow!("json parsing error {}", e))?;
    let product_bundle =
        images.0.iter().find(|i| i.name == "product_bundle").map(|i| i.path.clone());
    let product_bundle_path = if let Some(pb) = product_bundle {
        build_path.join(pb)
    } else {
        ffx_bail!(
            "Could not find the Product Bundle in the SDK. Build a product (`fx build`) and retry."
        );
    };
    let mut entries = Entries::new();
    let file = File::open(&product_bundle_path)?;
    entries.add_json(&mut BufReader::new(file))?;
    Ok(entries)
}

#[cfg(test)]
mod tests {
    use {super::*, tempfile::NamedTempFile};

    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_pbm() {
        let temp_file = NamedTempFile::new().expect("temp file");
        update_refresh_token(&temp_file.path()).await.expect("set refresh token");
    }
}
