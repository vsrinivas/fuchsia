// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    async_trait::async_trait,
    blobfs::Client,
    fidl_fuchsia_io::{DirectoryProxy, OPEN_RIGHT_READABLE},
    fidl_test_security_pkg::{PackageServer_Request, PackageServer_RequestStream},
    files_async::readdir,
    fuchsia_archive::Reader as FarReader,
    fuchsia_async::Task,
    fuchsia_component::server::ServiceFs,
    fuchsia_merkle::{Hash, MerkleTree},
    fuchsia_pkg_testing::{Package, PackageBuilder, RepositoryBuilder},
    fuchsia_syslog::{fx_log_info, init},
    fuchsia_url::pkg_url::PkgUrl,
    futures::{
        channel::oneshot::{channel, Receiver, Sender},
        stream::StreamExt,
        FutureExt,
    },
    io_util::{
        directory::{open_file, open_in_namespace},
        file::read,
    },
    std::{
        collections::HashMap,
        io::Cursor,
        net::Ipv4Addr,
        str::{from_utf8, FromStr},
        sync::Arc,
    },
    update_package::parse_packages_json,
};

/// Flags for pkg_server.
#[derive(FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path the directory containing the update package and all its
    /// packages.
    #[argh(option)]
    update_package_resource_directory: String,
}

// Facilitate unified loading of update package from package resources and other
// packages from blobfs.
#[async_trait]
trait PackageReader {
    async fn read(&self, merkle: &Hash) -> Vec<u8>;
}

struct BlobfsPackageReader {
    blobfs_client: Client,
}

impl BlobfsPackageReader {
    fn new(blobfs_client: Client) -> Self {
        Self { blobfs_client }
    }
}

#[async_trait]
impl PackageReader for BlobfsPackageReader {
    async fn read(&self, merkle: &Hash) -> Vec<u8> {
        read(&self.blobfs_client.open_blob_for_read(merkle).await.unwrap()).await.unwrap()
    }
}

struct UpdatePackageReader {
    merkle_root: Hash,
    package_contents: Vec<u8>,
    entries_directory: DirectoryProxy,
    entries_map: HashMap<Hash, String>,
}

impl UpdatePackageReader {
    fn new(
        merkle_root: Hash,
        package_contents: Vec<u8>,
        entries_directory: DirectoryProxy,
        entries_map: HashMap<Hash, String>,
    ) -> Self {
        Self { merkle_root, package_contents, entries_directory, entries_map }
    }

    async fn packages(&self) -> Vec<PkgUrl> {
        let mut packages_json_merkle = None;
        for (merkle, path) in self.entries_map.iter() {
            if path == "packages.json" {
                packages_json_merkle = Some(merkle);
                break;
            }
        }
        let packages_json_contents = self.read(packages_json_merkle.unwrap()).await;
        parse_packages_json(packages_json_contents.as_slice()).unwrap()
    }
}

#[async_trait]
impl PackageReader for UpdatePackageReader {
    async fn read(&self, merkle: &Hash) -> Vec<u8> {
        if &self.merkle_root == merkle {
            return self.package_contents.clone();
        }

        read(
            &open_file(
                &self.entries_directory,
                self.entries_map.get(merkle).unwrap(),
                OPEN_RIGHT_READABLE,
            )
            .await
            .unwrap(),
        )
        .await
        .unwrap()
    }
}

// Compute hashes of candidate files that may be part of the update package.
// Return the update package hash and a map from merkles to file names.
async fn map_update_package_directory(
    update_package_resource_directory: &DirectoryProxy,
) -> (Hash, HashMap<Hash, String>) {
    let mut map = HashMap::new();
    let mut update_package_merkle = None;
    for entry in readdir(update_package_resource_directory).await.unwrap().into_iter() {
        let file = open_file(update_package_resource_directory, &entry.name, OPEN_RIGHT_READABLE)
            .await
            .unwrap();
        let contents = read(&file).await.unwrap();
        let merkle = MerkleTree::from_reader(contents.as_slice()).unwrap().root();
        map.insert(merkle, entry.name.clone());

        if &entry.name == "update.far" {
            update_package_merkle = Some(merkle);
        }
    }
    (update_package_merkle.unwrap(), map)
}

// Open update package that has been "resourced" in a readable namespace
// directory. The update package is not included in the system blobfs where
// other packages can be found. As a workaround, an update package (including
// all its blobs) are bundled into this test's package as a collection of
// resources.
async fn open_resourced_update_package(
    update_package_resource_directory_proxy: DirectoryProxy,
) -> UpdatePackageReader {
    fx_log_info!("Opening resourced update package");
    let (update_package_merkle, update_package_map) =
        map_update_package_directory(&update_package_resource_directory_proxy).await;
    let update_package_file_name = update_package_map.get(&update_package_merkle).unwrap();
    let update_package_file = open_file(
        &update_package_resource_directory_proxy,
        update_package_file_name,
        OPEN_RIGHT_READABLE,
    )
    .await
    .unwrap();
    let update_package_contents = read(&update_package_file).await.unwrap();
    UpdatePackageReader::new(
        update_package_merkle,
        update_package_contents,
        update_package_resource_directory_proxy,
        update_package_map,
    )
}

// Parse string containing lines of the form `[path]=[merkle]`.
fn parse_path_merkle(contents: &str) -> Vec<(String, Hash)> {
    let lines = contents.split('\n');
    let mut path_merkles = vec![];
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let mut pair = line.splitn(2, "=");
        path_merkles
            .push((pair.next().unwrap().into(), Hash::from_str(pair.next().unwrap()).unwrap()));
    }
    path_merkles
}

async fn build_package(package_reader: &dyn PackageReader, pkg_url: &PkgUrl) -> Package {
    let pkg_contents = package_reader.read(pkg_url.package_hash().unwrap()).await;
    let mut pkg_contents_reader = FarReader::new(Cursor::new(pkg_contents.clone())).unwrap();
    let mut pkg_builder = PackageBuilder::new(pkg_url.name().as_ref());

    fx_log_info!("Loading meta files for package {}", pkg_url);
    for meta_file_entry in FarReader::new(Cursor::new(pkg_contents)).unwrap().list() {
        let meta_file_path = meta_file_entry.path();
        if meta_file_path == "meta/contents" || meta_file_path == "meta/package" {
            continue;
        }
        let meta_file_contents = pkg_contents_reader.read_file(meta_file_path).unwrap();
        pkg_builder = pkg_builder.add_resource_at(meta_file_path, meta_file_contents.as_slice());
    }

    fx_log_info!("Loading blobs for {}", pkg_url);
    let meta_contents = pkg_contents_reader.read_file("meta/contents").unwrap();
    let blob_path_merkles = parse_path_merkle(from_utf8(meta_contents.as_slice()).unwrap());
    for (path, merkle) in blob_path_merkles.into_iter() {
        let blob_contents = package_reader.read(&merkle).await;
        pkg_builder = pkg_builder.add_resource_at(path, blob_contents.as_slice());
    }

    pkg_builder.build().await.unwrap()
}

fn serve_package_server_protocol(url_recv: Receiver<String>, shutdown_send: Sender<()>) {
    let local_url = url_recv.shared();
    Task::spawn(async move {
        fx_log_info!("Preparing to serve test.security.pkg.PackageServer");
        let mut fs = ServiceFs::new();
        fs.dir("svc").add_fidl_service(move |mut stream: PackageServer_RequestStream| {
            let local_url = local_url.clone();
            fx_log_info!("New connection to test.security.pkg.PackageServer");
            Task::spawn(async move {
                fx_log_info!("Received test.security.pkg.PackageServer request");
                match stream.next().await.unwrap().unwrap() {
                    PackageServer_Request::GetUrl { responder } => {
                        let local_url = local_url.await.unwrap();
                        fx_log_info!(
                            "Responding to test.security.pkg.PackageServer.GetUrl request with {}",
                            local_url
                        );
                        responder.send(&local_url).unwrap();
                    }
                }
            })
            .detach();
        });
        fs.take_and_serve_directory_handle().unwrap();
        fs.collect::<()>().await;
        shutdown_send.send(()).unwrap();
    })
    .detach()
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    init().unwrap();
    fx_log_info!("Starting pkg_server");
    let args @ Args { update_package_resource_directory } = &argh::from_env();
    fx_log_info!("Initalizing pkg_server with {:?}", args);

    let (url_send, url_recv) = channel();
    let (shutdown_send, shutdown_recv) = channel();
    serve_package_server_protocol(url_recv, shutdown_send);

    let update_package_resource_directory_proxy =
        open_in_namespace(update_package_resource_directory, OPEN_RIGHT_READABLE).unwrap();
    let update_package_reader =
        open_resourced_update_package(update_package_resource_directory_proxy).await;
    let mut repository_builder = RepositoryBuilder::new();
    repository_builder = repository_builder.add_package(
        build_package(
            &update_package_reader,
            &PkgUrl::parse(&format!(
                "fuchsia-pkg://fuchsia.com/update/0?hash={}",
                update_package_reader.merkle_root,
            ))
            .unwrap(),
        )
        .await,
    );

    let blobfs_client = Client::open_from_namespace().unwrap();
    let blobfs_package_reader = BlobfsPackageReader::new(blobfs_client);
    for pkg_url in update_package_reader.packages().await.into_iter() {
        repository_builder =
            repository_builder.add_package(build_package(&blobfs_package_reader, &pkg_url).await);
    }

    let repository = Arc::new(repository_builder.build().await.unwrap())
        .server()
        .bind_to_addr(Ipv4Addr::LOCALHOST)
        .bind_to_port(443)
        .start()
        .unwrap();

    let local_url = repository.local_url();
    fx_log_info!("Served repository ready at {}", local_url);
    url_send.send(local_url).unwrap();
    shutdown_recv.await.unwrap();

    fx_log_info!("Package server shutting down");
}
