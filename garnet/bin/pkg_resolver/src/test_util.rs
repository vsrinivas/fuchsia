// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_amber::ControlRequest;
use fidl_fuchsia_io::DirectoryProxy;
use fidl_fuchsia_pkg::{self, PackageCacheRequest};
use fidl_fuchsia_pkg_ext::BlobId;
use fuchsia_async as fasync;
use fuchsia_zircon::{Channel, Peered, Signals, Status};
use serde::Serialize;
use serde_json;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io;
use std::rc::Rc;
use std::str;
use tempfile::TempDir;

pub(crate) fn create_dir<'a, T, S>(iter: T) -> tempfile::TempDir
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

pub(crate) struct Package {
    name: String,
    variant: String,
    merkle: String,
    kind: PackageKind,
}

pub(crate) enum PackageKind {
    Ok,
    Error(String),
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

pub(crate) struct MockAmber {
    packages: HashMap<(String, String), Package>,
    pkgfs: Rc<TempDir>,
    channels: Vec<Channel>,
}

impl MockAmber {
    pub(crate) fn new(packages: Vec<Package>, pkgfs: Rc<TempDir>) -> MockAmber {
        let mut package_map = HashMap::new();
        for package in packages {
            package_map.insert((package.name.clone(), package.variant.clone()), package);
        }
        MockAmber { packages: package_map, pkgfs, channels: vec![] }
    }

    pub(crate) fn get_update_complete(&mut self, req: ControlRequest) -> Result<(), Error> {
        match req {
            ControlRequest::GetUpdateComplete { name, version, responder, .. } => {
                let (s, c) = Channel::create().unwrap();
                let mut handles = vec![];
                let variant = version.unwrap_or_else(|| "0".to_string());
                if let Some(package) = self.packages.get(&(name, variant)) {
                    match package.kind {
                        PackageKind::Ok => {
                            // Create blob dir with a single file.
                            let blob_path = self.pkgfs.path().join(&package.merkle);
                            if let Err(e) = fs::create_dir(&blob_path) {
                                if e.kind() != io::ErrorKind::AlreadyExists {
                                    return Err(e.into());
                                }
                            }
                            let blob_file = blob_path.join(format!("{}_file", package.merkle));
                            fs::write(&blob_file, "hello")?;

                            s.write(package.merkle.as_bytes(), &mut handles)?;
                        }
                        PackageKind::Error(ref msg) => {
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
                self.channels.push(s);
                responder.send(c).expect("failed to send response");
            }
            _ => {}
        }
        Ok(())
    }
}

pub(crate) struct MockPackageCache {
    pkgfs: DirectoryProxy,
}

impl MockPackageCache {
    pub(crate) fn new(pkgfs: Rc<TempDir>) -> Result<MockPackageCache, Error> {
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
            _ => {}
        }
    }
}
