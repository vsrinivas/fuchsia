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
use fuchsia_pkg::{MetaContents, MetaPackage, PackageBuilder, PackageManifest};
use futures_lite::io::AsyncWriteExt;
use hyper::body::HttpBody;
use hyper::{StatusCode, Uri};
use serde_json::Value;
use std::fs::{metadata, File};
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
    let meta_contents = MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    let meta_package = meta_far.read_file("meta/package")?;
    let meta_package = MetaPackage::deserialize(meta_package.as_slice())?;

    let blob_output_path = output_path.join("blobs");
    if !blob_output_path.exists() {
        async_fs::create_dir_all(&blob_output_path).await?;
    }

    if blob_output_path.is_file() {
        ffx_bail!("Download path point to a file: {}", blob_output_path.display());
    }
    // Download all the blobs.
    let mut tasks = Vec::new();
    for hash in meta_contents.values() {
        let uri = format!("{}/{}", blob_url, hash).parse::<Uri>()?;
        let blob_path = blob_output_path.join(&hash.to_string());
        let client = Arc::clone(&client);
        tasks.push(async move { download_file_to_destination(uri, &client, blob_path).await });
    }
    futures::future::join_all(tasks).await;

    // Build the PackageManifest of this package.
    let mut package_builder = PackageBuilder::from_meta_package(meta_package);
    for (blob_path, hash) in meta_contents.iter() {
        let source_path = blob_output_path.join(&hash.to_string()).canonicalize()?;
        let size = metadata(&source_path)?.len();
        package_builder.add_entry(blob_path.to_string(), *hash, source_path, size);
    }
    let package = package_builder.build()?;
    let package_manifest = PackageManifest::from_package(package)?;
    let package_manifest_path = output_path.join("package_manifest.json");
    let mut file = async_fs::File::create(package_manifest_path).await?;
    file.write_all(serde_json::to_string(&package_manifest)?.as_bytes()).await?;
    file.sync_all().await?;
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
    let mut res = client.get(uri.clone()).await?;
    let status = res.status();
    if status != StatusCode::OK {
        ffx_bail!(
            "Cannot download file to {}. Status is {}. Uri is: {}. \n",
            destination.display(),
            status,
            &uri
        );
    }
    let mut output = async_fs::File::create(destination).await?;
    while let Some(next) = res.data().await {
        let chunk = next?;
        output.write_all(&chunk).await?;
    }
    output.sync_all().await?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{
        repository::{RepositoryManager, RepositoryServer},
        test_utils::make_writable_empty_repository,
    };
    use fuchsia_async as fasync;
    use fuchsia_pkg::CreationManifest;
    use fuchsia_pkg::{build_with_file_system, FileSystem};
    use maplit::{btreemap, hashmap};
    use std::collections::HashMap;
    use std::fs::create_dir;
    use std::io;
    use std::io::Write;
    use std::net::Ipv4Addr;

    struct FakeFileSystem {
        content_map: HashMap<String, Vec<u8>>,
    }

    impl<'a> FileSystem<'a> for FakeFileSystem {
        type File = &'a [u8];
        fn open(&'a self, path: &str) -> Result<Self::File, io::Error> {
            Ok(self.content_map.get(path).unwrap().as_slice())
        }
        fn len(&self, path: &str) -> Result<u64, io::Error> {
            Ok(self.content_map.get(path).unwrap().len() as u64)
        }
        fn read(&self, path: &str) -> Result<Vec<u8>, io::Error> {
            Ok(self.content_map.get(path).unwrap().clone())
        }
    }

    fn create_meta_far(path: PathBuf) {
        let creation_manifest = CreationManifest::from_external_and_far_contents(
            btreemap! {
                "lib/mylib.so".to_string() => "host/mylib.so".to_string()
            },
            btreemap! {
                "meta/my_component.cmx".to_string() => "host/my_component.cmx".to_string(),
                "meta/package".to_string() => "host/meta/package".to_string()
            },
        )
        .unwrap();
        let component_manifest_contents = "my_component.cmx contents";
        let mut v = vec![];
        let meta_package = MetaPackage::from_name_and_variant(
            "my-package-name".parse().unwrap(),
            "my-package-variant".parse().unwrap(),
        );
        meta_package.serialize(&mut v).unwrap();
        let file_system = FakeFileSystem {
            content_map: hashmap! {
                "host/mylib.so".to_string() => Vec::new(),
                "host/my_component.cmx".to_string() => component_manifest_contents.as_bytes().to_vec(),
                "host/meta/package".to_string() => v
            },
        };

        build_with_file_system(&creation_manifest, &path, "my-package-name", &file_system).unwrap();
    }

    fn create_targets_json() -> Vec<u8> {
        "{\"signed\":{\"targets\":{\"test_package\":{\"custom\":{\"merkle\":\"0000000000000000000000000000000000000000000000000000000000000000\"}}}}}".as_bytes().to_vec()
    }

    fn write_file(path: PathBuf, body: &[u8]) {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write(body).unwrap();
        tmp.persist(path).unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_download_package() {
        let manager = RepositoryManager::new();

        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path().join("tuf");
        let repo = make_writable_empty_repository("tuf", root.clone()).await.unwrap();

        // Write targets.json
        let target_json = create_targets_json();
        let target_json_path = root.join("targets.json");
        write_file(target_json_path.clone(), target_json.as_slice());

        let blob_dir = root.join("blobs");
        create_dir(&blob_dir).unwrap();

        // Put meta.far and blob into blobs directory
        let meta_far_path =
            blob_dir.join("0000000000000000000000000000000000000000000000000000000000000000");
        create_meta_far(meta_far_path);

        let blob_path =
            blob_dir.join("15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b");
        write_file(blob_path, "".as_bytes());

        manager.add(Arc::new(repo));

        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, server) =
            RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

        // Run the server in the background.
        let task = fasync::Task::local(server_fut);

        let tuf_url = server.local_url() + "/tuf";
        let blob_url = server.local_url() + "/tuf/blobs";

        let result_dir = tempdir.path().join("results");
        create_dir(&result_dir).unwrap();
        package_download(tuf_url, blob_url, String::from("test_package"), result_dir.clone())
            .await
            .unwrap();

        let result_package_manifest =
            std::fs::read_to_string(result_dir.join("package_manifest.json")).unwrap();
        assert!(result_package_manifest
            .contains("15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"));

        // Signal the server to shutdown.
        server.stop();

        // Wait for the server to actually shut down.
        task.await;
    }
}
