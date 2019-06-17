// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::amber_connector::AmberConnect;
use failure::Error;
use fidl::endpoints::{self, ServerEnd};
use fidl_fuchsia_amber::{
    ControlMarker as AmberMarker, ControlProxy as AmberProxy, ControlRequest,
    OpenedRepositoryMarker, OpenedRepositoryRequest,
};
use fidl_fuchsia_io::DirectoryProxy;
use fidl_fuchsia_pkg::{self, PackageCacheRequest, RepositoryConfig};
use fidl_fuchsia_pkg_ext::BlobId;
use fuchsia_async as fasync;
use fuchsia_zircon::{Channel, Peered, Signals, Status};
use futures::stream::TryStreamExt;
use parking_lot::Mutex;
use serde::Serialize;
use serde_json;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io;
use std::str;
use std::sync::Arc;
use tempfile::{self, TempDir};

pub(crate) fn create_dir<'a, T, S>(iter: T) -> TempDir
where
    T: IntoIterator<Item = (&'a str, S)>,
    S: Serialize,
{
    let dir = tempfile::tempdir().unwrap();

    for (name, config) in iter {
        let path = dir.path().join(name);
        let f = File::create(path).unwrap();
        serde_json::to_writer(f, &config).unwrap();
    }

    dir
}

#[derive(Debug)]
pub(crate) struct Package {
    name: String,
    variant: String,
    merkle: String,
    kind: PackageKind,
}

#[derive(Debug, PartialEq)]
pub(crate) enum PackageKind {
    Ok,
    Error(Status, String),
}

impl Package {
    pub(crate) fn new(name: &str, variant: &str, merkle: &str, kind: PackageKind) -> Self {
        Self {
            name: name.to_string(),
            variant: variant.to_string(),
            merkle: merkle.to_string(),
            kind: kind,
        }
    }
}

#[derive(Debug)]
pub(crate) struct MockAmber {
    source: Repository,
    repos: HashMap<String, (RepositoryConfig, Arc<Repository>)>,
}

impl MockAmber {
    fn spawn(self: Arc<Self>) -> AmberProxy {
        endpoints::spawn_local_stream_handler(move |req| {
            let a = self.clone();
            async move { await!(a.process_amber_request(req)) }
        })
        .expect("failed to spawn handler")
    }

    async fn process_amber_request(&self, req: ControlRequest) {
        match req {
            ControlRequest::GetUpdateComplete { name, version, responder, .. } => {
                let c = self.get_update_complete(name, version).unwrap();
                responder.send(c).expect("failed to send response");
            }
            ControlRequest::OpenRepository { config, repo, responder } => {
                self.open_repository(config, repo);
                responder.send(Status::OK.into_raw()).expect("failed to send response");
            }
            _ => panic!("not implemented: {:?}", req),
        }
    }

    fn get_update_complete(&self, name: String, version: Option<String>) -> Result<Channel, Error> {
        let (s, c) = Channel::create()?;
        let mut handles = vec![];
        let variant = version.unwrap_or_else(|| "0".to_string());
        if let Some(package) = self.source.get(name, variant)? {
            match package.kind {
                PackageKind::Ok => {
                    s.write(package.merkle.as_bytes(), &mut handles)?;
                }
                PackageKind::Error(_, ref msg) => {
                    // Package not found, signal error.
                    s.signal_peer(Signals::NONE, Signals::USER_0)?;
                    s.write(msg.as_bytes(), &mut handles)?;
                }
            }
        } else {
            // Package not found, signal error.
            s.signal_peer(Signals::NONE, Signals::USER_0)?;
            s.write("merkle not found for package ".as_bytes(), &mut handles)?;
        }

        Ok(c)
    }

    fn open_repository(&self, config: RepositoryConfig, repo: ServerEnd<OpenedRepositoryMarker>) {
        let opened_repo = if let Some(ref repo_url) = config.repo_url {
            let (opened_config, opened_repo) = self.repos.get(&**repo_url).expect("repo to exist");
            assert_eq!(&config, opened_config);
            opened_repo.clone()
        } else {
            panic!("repo does not exist");
        };

        fasync::spawn(async move {
            let mut stream = repo.into_stream().unwrap();

            while let Some(req) = await!(stream.try_next()).unwrap() {
                Self::process_repo_request(&opened_repo, req).expect("amber failed");
            }
        });
    }

    fn process_repo_request(repo: &Repository, req: OpenedRepositoryRequest) -> Result<(), Error> {
        match req {
            OpenedRepositoryRequest::GetUpdateComplete {
                name,
                variant,
                merkle: _,
                result,
                control_handle: _,
            } => {
                let variant = variant.unwrap_or_else(|| "0".to_string());

                let (_, result_control_channel) = result.into_stream_and_control_handle()?;

                if let Some(package) = repo.get(name, variant)? {
                    match package.kind {
                        PackageKind::Ok => {
                            result_control_channel.send_on_success(&package.merkle)?;
                        }
                        PackageKind::Error(status, ref msg) => {
                            result_control_channel.send_on_error(status.into_raw(), msg)?;
                        }
                    }
                } else {
                    result_control_channel.send_on_error(
                        Status::NOT_FOUND.into_raw(),
                        "merkle not found for package",
                    )?;
                }

                Ok(())
            }
        }
    }
}

#[derive(Debug)]
pub(crate) struct MockAmberBuilder {
    pkgfs: Arc<TempDir>,
    packages: Vec<Package>,
    repos: HashMap<String, (RepositoryConfig, Arc<Repository>)>,
}

impl MockAmberBuilder {
    pub(crate) fn new(pkgfs: Arc<TempDir>) -> Self {
        MockAmberBuilder { pkgfs, packages: vec![], repos: HashMap::new() }
    }

    pub(crate) fn packages<I: IntoIterator<Item = Package>>(mut self, packages: I) -> Self {
        self.packages.extend(packages);
        self
    }

    pub(crate) fn repo(mut self, config: RepositoryConfig, packages: Vec<Package>) -> Self {
        let repo_url = config.repo_url.as_ref().unwrap();
        let repo = Arc::new(Repository::new(self.pkgfs.clone(), packages));
        self.repos.insert(repo_url.to_string(), (config, repo));
        self
    }

    pub(crate) fn build(self) -> MockAmber {
        MockAmber { source: Repository::new(self.pkgfs, self.packages), repos: self.repos }
    }
}

#[derive(Debug)]
pub(crate) struct Repository {
    pkgfs: Arc<TempDir>,
    packages: Arc<HashMap<(String, String), Package>>,
}

impl Repository {
    fn new(pkgfs: Arc<TempDir>, packages: Vec<Package>) -> Self {
        let mut package_map = HashMap::new();
        for package in packages {
            package_map.insert((package.name.clone(), package.variant.clone()), package);
        }

        Repository { pkgfs, packages: Arc::new(package_map) }
    }

    fn get(&self, name: String, variant: String) -> Result<Option<&Package>, Error> {
        if let Some(package) = self.packages.get(&(name, variant)) {
            if package.kind == PackageKind::Ok {
                // Create blob dir with a single file.
                let blob_path = self.pkgfs.path().join(&package.merkle);
                if let Err(e) = fs::create_dir(&blob_path) {
                    if e.kind() != io::ErrorKind::AlreadyExists {
                        return Err(e.into());
                    }
                }
                let blob_file = blob_path.join(format!("{}_file", package.merkle));
                fs::write(&blob_file, "hello")?;
            }

            Ok(Some(package))
        } else {
            Ok(None)
        }
    }
}

pub(crate) struct MockPackageCache {
    pkgfs: DirectoryProxy,
}

impl MockPackageCache {
    pub(crate) fn new(pkgfs: Arc<TempDir>) -> Result<MockPackageCache, Error> {
        let f = File::open(pkgfs.path())?;
        let pkgfs = DirectoryProxy::new(fasync::Channel::from_channel(fdio::clone_channel(&f)?)?);
        Ok(MockPackageCache { pkgfs })
    }

    pub(crate) fn open(&self, req: PackageCacheRequest) {
        // Forward request to pkgfs directory.
        // FIXME: this is a bit of a hack but there isn't a formal way to convert a Directory
        // request into a Node request.
        match req {
            PackageCacheRequest::Open { meta_far_blob_id, dir, responder, .. } => {
                let node_request = ServerEnd::new(dir.into_channel());
                let flags =
                    fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
                let merkle = BlobId::from(meta_far_blob_id.merkle_root).to_string();
                let status = match self.pkgfs.open(flags, 0, &merkle, node_request) {
                    Ok(()) => Status::OK,
                    Err(e) => {
                        eprintln!("Cache lookup failed: {}", e);
                        Status::INTERNAL
                    }
                };
                responder.send(status.into_raw()).expect("failed to send response");
            }
            _ => panic!("not implemented: {:?}", req),
        }
    }
}

#[derive(Clone, Debug)]
pub(crate) struct MockAmberConnector {
    amber: Arc<Mutex<Arc<MockAmber>>>,
}

impl MockAmberConnector {
    pub(crate) fn new(amber: MockAmber) -> Self {
        let amber = Arc::new(Mutex::new(Arc::new(amber)));
        MockAmberConnector { amber }
    }

    pub(crate) fn set_amber(&mut self, amber: MockAmber) {
        *self.amber.lock() = Arc::new(amber);
    }
}

impl AmberConnect for MockAmberConnector {
    fn connect(&self) -> Result<AmberProxy, Status> {
        Ok(self.amber.lock().clone().spawn())
    }
}

#[derive(Debug, Clone)]
pub(crate) struct CloneAmberConnector {
    amber: AmberProxy,
}

impl CloneAmberConnector {
    pub(crate) fn new(amber: AmberProxy) -> Self {
        Self { amber }
    }
}

impl AmberConnect for CloneAmberConnector {
    fn connect(&self) -> Result<AmberProxy, fuchsia_zircon::Status> {
        Ok(self.amber.clone())
    }
}

#[derive(Debug, Clone)]
pub(crate) struct FailAmberConnector;

impl AmberConnect for FailAmberConnector {
    fn connect(&self) -> Result<AmberProxy, Status> {
        Err(Status::INTERNAL)
    }
}

#[derive(Debug, Clone)]
pub(crate) struct ClosedAmberConnector;

impl AmberConnect for ClosedAmberConnector {
    fn connect(&self) -> Result<AmberProxy, Status> {
        let (proxy, _) = fidl::endpoints::create_proxy::<AmberMarker>().unwrap();
        Ok(proxy)
    }
}
