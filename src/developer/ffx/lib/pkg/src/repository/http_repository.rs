// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools to download a Fuchsia package from from a TUF repository.
//! See
//! - [Package](https://fuchsia.dev/fuchsia-src/concepts/packages/package?hl=en)
//! - [TUF](https://theupdateframework.io/)

use {
    super::{Error, Repository, RepositoryBackend, Resource},
    anyhow::{anyhow, Context, Result},
    errors::{ffx_bail, ffx_error},
    fidl_fuchsia_developer_bridge_ext::RepositorySpec,
    fuchsia_hyper::new_https_client,
    fuchsia_pkg::{MetaContents, MetaPackage, PackageBuilder, PackageManifest},
    futures::TryStreamExt,
    futures_lite::io::{copy, AsyncWriteExt},
    hyper::body::HttpBody,
    hyper::{StatusCode, Uri},
    serde_json::Value,
    std::fs::{metadata, File},
    std::path::PathBuf,
    std::sync::Arc,
    std::time::SystemTime,
    tuf::{
        interchange::Json,
        repository::{HttpRepositoryBuilder as TufHttpRepositoryBuilder, RepositoryProvider},
    },
    url::Url,
};

#[derive(Debug)]
pub struct HttpRepository {
    repo_url: Url,
    blobs_url: Url,
}

impl HttpRepository {
    pub fn new(repo_url: Url, blobs_url: Url) -> Self {
        Self { repo_url, blobs_url }
    }

    async fn fetch_from(&self, root: &Url, resource_path: &str) -> Result<Resource, Error> {
        let client = new_https_client();
        let full_url = root.join(resource_path).map_err(|e| anyhow!(e))?;
        let uri = full_url.as_str().parse::<Uri>().map_err(|e| anyhow!(e))?;
        let resp =
            client.get(uri).await.context(format!("fetching resource {}", full_url.as_str()))?;
        let body = match resp.status() {
            StatusCode::OK => resp.into_body(),
            StatusCode::NOT_FOUND => return Err(Error::NotFound),
            status_code => {
                return Err(Error::Other(anyhow!(
                    "Got error downloading resource, error is: {}",
                    status_code
                )))
            }
        };
        Ok(Resource {
            len: body.size_hint().exact(),
            stream: Box::pin(body.map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))),
        })
    }
}

#[async_trait::async_trait]
impl RepositoryBackend for HttpRepository {
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::HttpRepository {
            repo_url: self.repo_url.as_str().to_owned(),
            blobs_url: self.blobs_url.as_str().to_owned(),
        }
    }

    async fn fetch(&self, resource_path: &str) -> Result<Resource, Error> {
        self.fetch_from(&self.repo_url, resource_path).await
    }

    async fn fetch_blob(&self, resource_path: &str) -> Result<Resource, Error> {
        self.fetch_from(&self.blobs_url, resource_path).await
    }

    fn get_tuf_repo(&self) -> Result<Box<(dyn RepositoryProvider<Json> + 'static)>, Error> {
        Ok(Box::new(
            TufHttpRepositoryBuilder::<_, Json>::new(
                self.repo_url.clone().into(),
                new_https_client(),
            )
            .build(),
        ) as Box<dyn RepositoryProvider<Json>>)
    }

    async fn target_modification_time(&self, _path: &str) -> Result<Option<SystemTime>> {
        Ok(None)
    }
}

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
    let backend = Box::new(HttpRepository::new(Url::parse(&tuf_url)?, Url::parse(&blob_url)?));
    let repo = Repository::new("repo", backend).await?;

    let desc = repo
        .get_target_description(&target_path)
        .await?
        .context("missing target description here")?
        .custom()
        .context("missing custom data")?
        .get("merkle")
        .context("missing merkle")?
        .clone();
    let merkle = if let Value::String(hash) = desc {
        hash.to_string()
    } else {
        ffx_bail!("[Error] Merkle field is not a String. {:#?}", desc);
    };

    if !output_path.exists() {
        async_fs::create_dir_all(&output_path).await?;
    }

    if output_path.is_file() {
        ffx_bail!("Download path point to a file: {}", output_path.display());
    }
    let meta_far_path = output_path.join("meta.far");

    download_blob_to_destination(&merkle, &repo, meta_far_path.clone()).await?;

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
    let repo = Arc::new(repo);
    for hash in meta_contents.values() {
        let blob_path = blob_output_path.join(&hash.to_string());
        let clone = repo.clone();
        tasks.push(async move {
            download_blob_to_destination(&hash.to_string(), &clone, blob_path).await
        });
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

/// Download a blob from the repository and save it to the given
/// destination
/// `path`: Path on the server from which to download the package.
/// `repo`: A [Repository] instance.
/// `destination`: Local path to save the downloaded package.
async fn download_blob_to_destination(
    path: &str,
    repo: &Repository,
    destination: PathBuf,
) -> Result<()> {
    let res = repo.fetch_blob(path).await.map_err(|e| {
        ffx_error!("Cannot download file to {}. Error was {}", destination.display(), e)
    })?;
    copy(res.stream.into_async_read(), async_fs::File::create(destination).await?).await?;
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

        build_with_file_system(&creation_manifest, &path, "my-package-name", &file_system).unwrap();
    }

    fn create_timestamp_json() -> Vec<u8> {
        "{\"signatures\":[{\"keyid\":\"f434c3e5c9056b91416d571deb6c37670b802b0f5df52daee7466ca6514f73a2\",\"sig\":\"4f768973859549e0a3af9cd7ea44c2a10f976294af2e5e7bdc290edafbaaaa534ea51a8d2c5822724e532e0a1ce168c8315e43a543f7a273a5bb4e26946b8b08\"}],\"signed\":{\"_type\":\"timestamp\",\"expires\":\"2021-08-14T21:10:55Z\",\"meta\":{\"snapshot.json\":{\"hashes\":{\"sha256\":\"710a28f6215a7b16c4807b2db5a243d9f6e7cda744438e0f2b99519fdbb84257\",\"sha512\":\"42967b951a58cb6ecd78f96d1c418114232f8df77c023790d74cb6f92fc13f0ff8ad7322b2543953e744f8305ea894556a64f167c395f449f6523d8fb07640e5\"},\"length\":604,\"version\":2}},\"spec_version\":\"1.0\",\"version\":2}}".as_bytes().to_vec()
    }

    fn create_snapshot_json() -> Vec<u8> {
        "{\"signatures\":[{\"keyid\":\"6a0cb8694c6d532b30d04d0e9d7b024f18be8a471bd7902dc4931f48681669b2\",\"sig\":\"ee748c85aa985c14da0b8a9414111523c962d1f45151ca647bd62eb68db88086a80b4d8611e7d834b59d33e2445c3778892da7755062faa3430a4f30c0271908\"}],\"signed\":{\"_type\":\"snapshot\",\"expires\":\"2071-07-30T21:10:55Z\",\"meta\":{\"targets.json\":{\"hashes\":{\"sha256\":\"a20d016c49e2429d051768330f3a6d58dd9f71565447d598568cd2a06311b929\",\"sha512\":\"846b17dfacfbcbfb06db9c1bf5483529c8a35e4770f43e36ec4e732b1f7c3e508103e31fd8991502227429c97a1e36b8666135001c419d7de2b3e7b82c2821ab\"},\"length\":732,\"version\":2}},\"spec_version\":\"1.0\",\"version\":2}}".as_bytes().to_vec()
    }

    fn create_targets_json() -> Vec<u8> {
        "{\"signatures\":[{\"keyid\":\"8be84e2588cccaf03c63142b13cbc0502d102e38de08b90308d894f64401551e\",\"sig\":\"104922a7f892ba942ff31e693a65720db59a545647cd204da99f3441b9efbf347308d30c088ee04d1e35f1b4eaff13a640929a29e08306c66d01f8207eac980a\"}],\"signed\":{\"_type\":\"targets\",\"custom\":{\"fuchsia_spec_version\":1},\"expires\":\"2071-07-30T21:10:54Z\",\"spec_version\":\"1.0\",\"targets\":{\"test_package\":{\"custom\":{\"merkle\":\"947015aca61730b5035469d86344aa9d68284143967e41496a9394b26ac8eabc\",\"size\":16384},\"hashes\":{\"sha256\":\"c2ddac678eb28380111b40a322bef4d3e04814ceef3848927aa7cbe6078a7272\",\"sha512\":\"5aef8da41e1494238f1ff09b61788201aece38b13fce66be8ebf60c55c7756ce449ae811a47e4c84ba0628ba36aaf5abfa65604ef822a686cbef52bb26c98f95\"},\"length\":16384}},\"version\":2}}".as_bytes().to_vec()
    }

    fn create_root_json() -> Vec<u8> {
        "{\"signatures\":[{\"keyid\":\"dfd295f01f950ff727b1bccdb7bfc89a70507d1096eaa9ddd9179efc377929a3\",\"sig\":\"820bb8b3b8481711bc05a22e92cd8b93b0c1d883126f51cf8c5269ef0b6b1540a67e35b1215f25ff84a75bbbb3131b103a025decd1f0bda1e0e0078b4419f803\"}],\"signed\":{\"_type\":\"root\",\"consistent_snapshot\":true,\"expires\":\"2031-07-30T21:06:41Z\",\"keys\":{\"6a0cb8694c6d532b30d04d0e9d7b024f18be8a471bd7902dc4931f48681669b2\":{\"keytype\":\"ed25519\",\"keyval\":{\"public\":\"f343a886cb113b6d114870fcde55658fda70e1d867ae522242c686562e000782\"},\"scheme\":\"ed25519\"},\"8be84e2588cccaf03c63142b13cbc0502d102e38de08b90308d894f64401551e\":{\"keytype\":\"ed25519\",\"keyval\":{\"public\":\"4174358c0a5c7cee348b2670a5aaaa40d434740e51aa3ce9483c62f14e4e7c48\"},\"scheme\":\"ed25519\"},\"dfd295f01f950ff727b1bccdb7bfc89a70507d1096eaa9ddd9179efc377929a3\":{\"keytype\":\"ed25519\",\"keyval\":{\"public\":\"4c66ec58bf1e2c75970f8d336bc55826742cdb5a0ad8f76cc21ecb6127092302\"},\"scheme\":\"ed25519\"},\"f434c3e5c9056b91416d571deb6c37670b802b0f5df52daee7466ca6514f73a2\":{\"keytype\":\"ed25519\",\"keyval\":{\"public\":\"358f5482ebbaf3892aab2d49be8863deb8c729856e7f6f826038b172eaf6cce2\"},\"scheme\":\"ed25519\"}},\"roles\":{\"root\":{\"keyids\":[\"dfd295f01f950ff727b1bccdb7bfc89a70507d1096eaa9ddd9179efc377929a3\"],\"threshold\":1},\"snapshot\":{\"keyids\":[\"6a0cb8694c6d532b30d04d0e9d7b024f18be8a471bd7902dc4931f48681669b2\"],\"threshold\":1},\"targets\":{\"keyids\":[\"8be84e2588cccaf03c63142b13cbc0502d102e38de08b90308d894f64401551e\"],\"threshold\":1},\"timestamp\":{\"keyids\":[\"f434c3e5c9056b91416d571deb6c37670b802b0f5df52daee7466ca6514f73a2\"],\"threshold\":1}},\"spec_version\":\"1.0\",\"version\":4}}".as_bytes().to_vec()
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

        // Write Metadata
        let root_json = create_root_json();
        write_file(root.join("root.json"), root_json.as_slice());
        let timestamp_json = create_timestamp_json();
        write_file(root.join("timestamp.json"), timestamp_json.as_slice());
        let snapshot_json = create_snapshot_json();
        write_file(root.join("2.snapshot.json"), snapshot_json.as_slice());
        let target_json = create_targets_json();
        write_file(root.join("2.targets.json"), target_json.as_slice());

        let blob_dir = root.join("blobs");
        create_dir(&blob_dir).unwrap();

        // Put meta.far and blob into blobs directory
        let meta_far_path =
            blob_dir.join("947015aca61730b5035469d86344aa9d68284143967e41496a9394b26ac8eabc");
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

        let tuf_url = server.local_url() + "/tuf/";
        let blob_url = server.local_url() + "/tuf/blobs/";

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
