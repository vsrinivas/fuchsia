// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_pdk_fetch_args::FetchCommand;
use ffx_pdk_lib::lock::Lock;
use fuchsia_hyper::{new_https_client, HttpsClient};
use futures_lite::io::AsyncWriteExt;
use hyper::body::HttpBody;
use hyper::{StatusCode, Uri};
use std::collections::{BTreeMap, HashSet};
use std::fs::{hard_link, remove_file, File};
use std::io::BufReader;
use std::path::PathBuf;
use std::sync::Arc;

const BLOB_URL: &str = "https://fuchsia-blobs.googleusercontent.com";

#[ffx_plugin("ffx_pdk")]
pub async fn cmd_update(cmd: FetchCommand) -> Result<()> {
    let client = Arc::new(new_https_client());
    let lock_file_path = cmd.lock_file;

    if cmd.show_progress {
        println!("Reading lock file from : {:#?}", &lock_file_path.display());
    }

    let artifact_lock: Lock =
        File::open(&lock_file_path).map(BufReader::new).map(serde_json::from_reader)??;

    let mut tasks = Vec::new();
    let blob_output_path = cmd.out.join("blobs");
    if !blob_output_path.exists() {
        async_fs::create_dir_all(&blob_output_path).await?;
    }
    let artifact_store_root_path = cmd.out.join("artifact_stores");
    let mut merkle_map = BTreeMap::new();

    // Dedup the blobs.
    let mut deduped_blobs = BTreeMap::<String, HashSet<String>>::new();
    for artifact in artifact_lock.artifacts {
        let blobs = artifact.blobs;
        let blob_hostname = match artifact.artifact_store.content_address_storage {
            Some(hostname) => hostname,
            None => BLOB_URL.to_string(),
        };

        let mut current_blob_set =
            deduped_blobs.get(&blob_hostname).map(|set| set.to_owned()).unwrap_or_default();

        current_blob_set.extend(blobs);

        deduped_blobs.insert(blob_hostname, current_blob_set);

        let artifact_store_path = artifact_store_root_path.join(artifact.artifact_store.name);
        async_fs::create_dir_all(&artifact_store_path).await?;
        let destination = artifact_store_path.join(artifact.name.replace("/0", ""));
        merkle_map.insert(artifact.merkle, destination);
    }

    for (cas, blobs) in deduped_blobs {
        for blob in blobs {
            let uri = format!("{}/{}", &cas, &blob).parse::<Uri>()?;
            let client = Arc::clone(&client);
            let destination = blob_output_path.join(&blob);
            if cmd.show_progress {
                println!("Downloading blob : {} to {}", &blob, &destination.display());
            }
            tasks
                .push(async move { download_file_to_destination(uri, &client, destination).await });
        }
    }

    futures::future::join_all(tasks).await;

    // Link the meta.far to corresponding blob.
    for (merkle, destination) in merkle_map {
        let src = blob_output_path.join(&merkle);
        if destination.exists() {
            remove_file(&destination)?;
        }
        if !src.exists() {
            ffx_bail!(
                "Cannot link {} to {} because blob file {} does not exist.",
                &src.display(),
                &destination.display(),
                &src.display()
            );
        }
        if cmd.show_progress {
            println!("Hard link from {} to {}", &src.display(), &destination.display());
        }
        hard_link(src, destination)?;
    }
    Ok(())
}

/// Download file and save it to the given destination.
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
    use fuchsia_async as fasync;
    use fuchsia_pkg::{build_with_file_system, CreationManifest, FileSystem, MetaPackage};
    use maplit::{btreemap, hashmap};
    use pkg::repository::{RepositoryManager, RepositoryServer};
    use pkg::test_utils::make_writable_empty_repository;
    use std::collections::HashMap;
    use std::fs::create_dir;
    use std::io;
    use std::io::Write;
    use std::net::Ipv4Addr;

    const META_FAR_MERKLE: &str =
        "0000000000000000000000000000000000000000000000000000000000000000";
    const EMTPY_BLOB_MERKLE: &str =
        "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";

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
        let meta_package =
            MetaPackage::from_name_and_variant("my-package-name", "my-package-variant").unwrap();
        meta_package.serialize(&mut v).unwrap();
        let file_system = FakeFileSystem {
            content_map: hashmap! {
                "host/mylib.so".to_string() => Vec::new(),
                "host/my_component.cmx".to_string() => component_manifest_contents.as_bytes().to_vec(),
                "host/meta/package".to_string() => v
            },
        };

        build_with_file_system(&creation_manifest, &path, "test_package", &file_system).unwrap();
    }

    fn write_file(path: PathBuf, body: &[u8]) {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write(body).unwrap();
        tmp.persist(path).unwrap();
    }

    fn create_artifact_lock(artifact_lock_path: PathBuf, blob_url: String) {
        let data = r#"
        {
            "artifacts":[
                {
                    "name": "test_package/0",
                    "type": "package",
                    "artifact_store": {
                        "name": "test_artifact_store",
                        "type": "tuf",
                        "repo": "mytest.repo",
                        "artifact_group_name": "f7d54e7a-7b65-4b52-8b6e-ccd9591952f0",
                        "content_address_storage": "{cas}"
                    },
                    "attributes": {
                        "version": "version_001",
                        "architecture": "x64",
                        "branch": "release",
                        "sdk_version": "6.20210819.3.1",
                        "creation_time": "2021-08-25T09:31:40.118779"
                    },
                    "merkle": "{meta_far}",
                    "blobs": [
                        "{meta_far}",
                        "{blob}"
                    ]
                }
            ]
        }
        "#;
        let data = data
            .replace("{cas}", &blob_url)
            .replace("{meta_far}", META_FAR_MERKLE)
            .replace("{blob}", EMTPY_BLOB_MERKLE);

        write_file(artifact_lock_path, data.as_bytes());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fetch_command() {
        let manager = RepositoryManager::new();

        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path().join("artifact_store");
        let repo = make_writable_empty_repository("artifact_store", root.clone()).await.unwrap();

        let blob_dir = root.join("blobs");
        create_dir(&blob_dir).unwrap();

        // Put meta.far and blob into blobs directory
        let meta_far_path = blob_dir.join(META_FAR_MERKLE);
        create_meta_far(meta_far_path);

        let blob_path = blob_dir.join(EMTPY_BLOB_MERKLE);
        write_file(blob_path, "".as_bytes());

        manager.add(Arc::new(repo));

        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, server) =
            RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

        // Run the server in the background.
        let task = fasync::Task::local(server_fut);

        let blob_url = server.local_url() + "/artifact_store/blobs";

        let artifact_lock_path = tempdir.path().join("artifact_lock.json");
        create_artifact_lock(artifact_lock_path.clone(), blob_url.clone());

        let result_dir = tempdir.path().join("results");
        create_dir(&result_dir).unwrap();

        let cmd = FetchCommand {
            lock_file: artifact_lock_path,
            out: result_dir.clone(),
            merkle: None,
            artifact: None,
            local_dir: None,
            show_progress: true,
        };
        cmd_update(cmd).await.unwrap();
        assert!(result_dir.join(format!("blobs/{}", META_FAR_MERKLE)).exists());
        assert!(result_dir.join(format!("blobs/{}", EMTPY_BLOB_MERKLE)).exists());

        assert!(result_dir.join("artifact_stores/test_artifact_store/test_package").exists());

        // Signal the server to shutdown.
        server.stop();

        // Wait for the server to actually shut down.
        task.await;
    }
}
