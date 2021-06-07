// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools to download a Fuchsia package from from a TUF repository.
//! See
//! - [Package](https://fuchsia.dev/fuchsia-src/concepts/packages/package?hl=en)
//! - [TUF](https://theupdateframework.io/)

use anyhow::Result;
use errors::ffx_bail;
use fuchsia_hyper::{new_https_client, HttpsClient};
use futures_lite::io::AsyncWriteExt;
use hyper::body::HttpBody;
use hyper::{StatusCode, Uri};
use serde_json::Value;
use std::fs::File;
use std::path::PathBuf;
use std::sync::Arc;

/// Download a package from a TUF repo.
///
/// `tuf_url`: The URL of the TUF repo.
/// `blob_url`: URL of Blobs Server.
/// `target_path`: Target path for the package to download.
/// `output_path`: Local path to save the downloaded package.
pub async fn package_download(
    tuf_url: String,
    blob_url: String,
    target_path: String,
    output_path: PathBuf,
) -> Result<()> {
    let client = Arc::new(new_https_client());

    // TODO(fxb/75396): Use rust-tuf to find the merkle for the package path
    let merkle = read_meta_far_merkle(tuf_url, &client, target_path).await?;

    let uri = format!("{}/{}", blob_url, merkle).parse::<Uri>()?;
    if !output_path.exists() {
        async_fs::create_dir_all(&output_path).await?;
    }

    if output_path.is_file() {
        ffx_bail!("Download path point to a file: {}", output_path.display());
    }
    let meta_far_path = output_path.join("meta.far");

    download_file_to_destination(uri, &client, meta_far_path.clone()).await?;

    let mut archive = File::open(&meta_far_path)?;
    let mut meta_far = fuchsia_archive::Reader::new(&mut archive)?;
    let meta_contents = meta_far.read_file("meta/contents")?;
    let meta_contents =
        fuchsia_pkg::MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    let mut tasks = Vec::new();
    for hash in meta_contents.values() {
        let uri = format!("{}/{}", blob_url, hash).parse::<Uri>()?;
        let blob_path = output_path.join(&hash.to_string());
        let client = Arc::clone(&client);
        tasks.push(async move { download_file_to_destination(uri, &client, blob_path).await });
    }
    futures::future::join_all(tasks).await;
    Ok(())
}

/// Check if the merkle of downloaded meta.far matches the merkle in targets.json
///
/// `tuf_url`: The URL of the TUF repo.
/// `client`: Https Client used to make request.
/// `target_path`: target path of package on TUF repo.
async fn read_meta_far_merkle(
    tuf_url: String,
    client: &HttpsClient,
    target_path: String,
) -> Result<String> {
    let uri = format!("{}/targets.json", tuf_url).parse::<Uri>()?;
    let dir = tempfile::tempdir()?;
    let path = dir.path().join("targets.json");

    download_file_to_destination(uri, &client, path.clone()).await?;
    let targets: Value = serde_json::from_reader(File::open(&path)?)?;
    let merkle = &targets["signed"]["targets"][&target_path]["custom"]["merkle"];
    if let Value::String(hash) = merkle {
        Ok(hash.to_string())
    } else {
        ffx_bail!("[Error] Merkle field is not a String. {:#?}", merkle);
    }
}

/// Download file and save it to the given
///
/// `uri`: Uri from where file is downloaded.
/// `client`: Https Client used to make request.
/// `destination`: Local path to save the downloaded package.
async fn download_file_to_destination(
    uri: Uri,
    client: &HttpsClient,
    destination: PathBuf,
) -> Result<()> {
    let mut res = client.get(uri).await?;
    let status = res.status();
    if status != StatusCode::OK {
        ffx_bail!("Cannot download file to {}. Status is {}", destination.display(), status);
    }
    let mut output = async_fs::File::create(destination).await?;
    while let Some(next) = res.data().await {
        let chunk = next?;
        output.write_all(&chunk).await?;
    }
    Ok(())
}
