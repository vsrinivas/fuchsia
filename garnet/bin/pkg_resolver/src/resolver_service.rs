// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServerEnd};
use fidl_fuchsia_amber::{self, ControlMarker as AmberMarker, ControlProxy as AmberProxy};
use fidl_fuchsia_io::{self, DirectoryMarker};
use fidl_fuchsia_pkg::{
    PackageCacheProxy, PackageResolverRequest, PackageResolverRequestStream, UpdatePolicy,
};
use fidl_fuchsia_pkg_ext::BlobId;
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use fuchsia_uri::pkg_uri::FuchsiaPkgUri;
use fuchsia_zircon::{Channel, MessageBuf, Signals, Status};
use futures::prelude::*;
use log::{info, warn};

pub async fn run_resolver_service(
    mut amber: AmberProxy,
    cache: PackageCacheProxy,
    chan: fasync::Channel,
) -> Result<(), Error> {
    let mut stream = PackageResolverRequestStream::from_channel(chan);

    let mut should_reconnect = false;

    while let Some(event) = await!(stream.try_next())? {
        let PackageResolverRequest::Resolve {
            package_uri,
            selectors,
            update_policy,
            dir,
            responder,
        } = event;

        if should_reconnect {
            info!("Reconnecting to amber.");
            amber = connect_to_service::<AmberMarker>().context("error connecting to amber")?;
            should_reconnect = false;
        }

        info!("Resolving {}.", package_uri);

        let status = await!(resolve(&amber, &cache, package_uri, selectors, update_policy, dir));

        // TODO this is an overbroad error type for this, make it more accurate
        if let Err(Status::INTERNAL) = &status {
            warn!("Resolution had an internal error, will reconnect to amber on next request.");
            should_reconnect = true;
        }

        responder.send(Status::from(status).into_raw())?;
    }

    Ok(())
}

/// Resolve the package.
///
/// FIXME: at the moment, we are proxying to Amber to resolve a package name and variant to a
/// merkleroot. Because of this, we cant' implement the update policy, so we just ignore it.
async fn resolve<'a>(
    amber: &'a AmberProxy,
    cache: &'a PackageCacheProxy,
    pkg_uri: String,
    selectors: Vec<String>,
    _update_policy: UpdatePolicy,
    dir_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status> {
    fx_log_info!("resolving {:?} with the selectors {:?}", pkg_uri, selectors);

    let uri = FuchsiaPkgUri::parse(&pkg_uri).map_err(|err| {
        fx_log_err!("failed to parse package uri {:?}: {}", pkg_uri, err);
        Err(Status::INVALID_ARGS)
    })?;

    // FIXME: at the moment only the fuchsia.com host is supported.
    if uri.host() != "fuchsia.com" {
        fx_log_warn!("package uri's host is currently unsupported: {}", uri);
    }

    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("resolve does not support selectors yet");
    }

    // While the fuchsia-pkg:// spec doesn't require a package name, we do.
    let name = uri.name().ok_or_else(|| {
        fx_log_err!("package uri is missing a package name: {}", uri);
        Err(Status::INVALID_ARGS)
    })?;

    // FIXME: use the package cache to fetch the package instead of amber.

    // Ask amber to cache the package.
    let chan =
        await!(amber.get_update_complete(&name, uri.variant(), uri.hash())).map_err(|err| {
            fx_log_err!("error communicating with amber: {:?}", err);
            Status::INTERNAL
        })?;

    let merkle = await!(wait_for_update_to_complete(chan)).map_err(|err| {
        fx_log_err!("error when waiting for amber to complete: {:?}", err);
        Status::INTERNAL
    })?;

    fx_log_info!("success: {} has a merkle of {}", name, merkle);

    await!(cache.open(&mut merkle.into(), &mut selectors.iter().map(|s| s.as_str()), dir_request))
        .map_err(|err| {
            fx_log_err!("error opening {}: {:?}", merkle, err);
            Status::INTERNAL
        })?;

    Ok(())
}

async fn wait_for_update_to_complete(chan: Channel) -> Result<BlobId, Status> {
    let mut buf = MessageBuf::new();

    let sigs = await!(fasync::OnSignals::new(
        &chan,
        Signals::CHANNEL_PEER_CLOSED | Signals::CHANNEL_READABLE
    ))?;

    if sigs.contains(Signals::CHANNEL_READABLE) {
        chan.read(&mut buf)?;
        let buf = buf.split().0;

        if sigs.contains(Signals::USER_0) {
            let msg = String::from_utf8_lossy(&buf);
            fx_log_err!("error installing package: {}", msg);
            return Err(Status::INTERNAL);
        }

        let merkle = match String::from_utf8(buf) {
            Ok(merkle) => merkle,
            Err(err) => {
                let merkle = String::from_utf8_lossy(err.as_bytes());
                fx_log_err!("{:?} is not a valid UTF-8 encoded merkleroot: {:?}", merkle, err);

                return Err(Status::INTERNAL);
            }
        };

        let merkle = match merkle.parse() {
            Ok(merkle) => merkle,
            Err(err) => {
                fx_log_err!("{:?} is not a valid merkleroot: {:?}", merkle, err);

                return Err(Status::INTERNAL);
            }
        };

        Ok(merkle)
    } else {
        fx_log_err!("response channel closed unexpectedly");
        Err(Status::INTERNAL)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use failure::Error;
    use fidl::endpoints::ServerEnd;
    use fidl_fuchsia_amber::{ControlRequest, ControlRequestStream};
    use fidl_fuchsia_io::DirectoryProxy;
    use fidl_fuchsia_pkg::{
        self, PackageCacheProxy, PackageCacheRequest, PackageCacheRequestStream, UpdatePolicy,
    };
    use files_async;
    use fuchsia_async as fasync;
    use fuchsia_zircon::{Channel, Handle, Peered, Signals, Status};
    use std::collections::HashMap;
    use std::fs::{self, File};
    use std::io;
    use std::path::Path;
    use std::rc::Rc;
    use std::str;
    use tempfile::TempDir;

    #[derive(PartialEq, Eq, Hash)]
    struct PackageId {
        name: String,
        variant: Option<String>,
        merkle: Option<String>,
    }

    impl PackageId {
        fn new_with_name(name: &str) -> PackageId {
            PackageId { name: name.to_string(), variant: None, merkle: None }
        }
        fn new_with_variant(name: &str, variant: &str) -> PackageId {
            PackageId { name: name.to_string(), variant: Some(variant.to_string()), merkle: None }
        }
        fn new_with_merkle(name: &str, variant: &str, merkle: &str) -> PackageId {
            PackageId {
                name: name.to_string(),
                variant: Some(variant.to_string()),
                merkle: Some(merkle.to_string()),
            }
        }
    }

    type PackageMap = HashMap<PackageId, String>;
    struct MockAmber {
        pkg_merkles: PackageMap,
        pkgfs: Rc<TempDir>,
        channels: Vec<Channel>,
    }

    impl MockAmber {
        fn new(pkg_merkles: PackageMap, pkgfs: Rc<TempDir>) -> MockAmber {
            MockAmber { pkg_merkles, pkgfs, channels: vec![] }
        }

        async fn run(&mut self, chan: fasync::Channel) -> Result<(), Error> {
            let mut stream = ControlRequestStream::from_channel(chan);
            while let Some(event) = await!(stream.try_next())? {
                match event {
                    ControlRequest::GetUpdateComplete { name, version, merkle, responder } => {
                        self.get_update_complete(name, version, merkle, responder)
                            .expect("GetUpdateComplete failed");
                    }
                    _ => {}
                }
            }

            Ok(())
        }

        fn get_update_complete(
            &mut self,
            name: String,
            variant: Option<String>,
            merkle: Option<String>,
            responder: fidl_fuchsia_amber::ControlGetUpdateCompleteResponder,
        ) -> Result<(), Error> {
            let (s, c) = Channel::create()?;
            let mut handles: Vec<Handle> = vec![];
            let key = PackageId { name, variant, merkle };
            if self.pkg_merkles.contains_key(&key) {
                // Create blob dir with a single file.
                let merkle = &self.pkg_merkles[&key];
                s.write(merkle.as_bytes(), &mut handles)?;
                let blob_path = self.pkgfs.path().join(merkle);
                match fs::create_dir(&blob_path) {
                    Ok(()) => Ok(()),
                    Err(e) => {
                        if e.kind() == io::ErrorKind::AlreadyExists {
                            Ok(())
                        } else {
                            Err(e)
                        }
                    }
                }?;
                let blob_file = blob_path.join(format!("{}_file", merkle));
                fs::write(&blob_file, "hello")?;
            } else {
                // Package not found, signal error.
                s.signal_peer(Signals::NONE, Signals::USER_0)?;
                s.write("update failed".as_bytes(), &mut handles)?;
            }
            self.channels.push(s);
            responder.send(c)?;
            Ok(())
        }
    }

    struct MockPackageCache {
        pkgfs: Rc<TempDir>,
    }

    impl MockPackageCache {
        fn new(pkgfs: Rc<TempDir>) -> MockPackageCache {
            MockPackageCache { pkgfs }
        }

        async fn run(&self, chan: fasync::Channel) -> Result<(), Error> {
            let mut stream = PackageCacheRequestStream::from_channel(chan);
            let f = File::open(self.pkgfs.path())?;
            let pkgfs =
                DirectoryProxy::new(fasync::Channel::from_channel(fdio::clone_channel(&f)?)?);
            while let Some(event) = await!(stream.try_next())? {
                match event {
                    PackageCacheRequest::Open {
                        meta_far_blob_id,
                        selectors: _selectors,
                        dir,
                        responder,
                    } => {
                        // Forward the directory handle to the corresponding blob directory in
                        // pkgfs.
                        let status = match await!(self.open(&pkgfs, meta_far_blob_id, dir)) {
                            Ok(()) => Status::OK,
                            Err(e) => {
                                eprintln!("Cache lookup failed: {}", e);
                                Status::INTERNAL
                            }
                        };
                        responder.send(status.into_raw())?;
                    }
                    _ => {}
                }
            }
            Ok(())
        }

        async fn open<'a>(
            &'a self,
            pkgfs: &'a DirectoryProxy,
            meta_far_blob_id: fidl_fuchsia_pkg::BlobId,
            dir: ServerEnd<DirectoryMarker>,
        ) -> Result<(), Error> {
            // Forward request to pkgfs directory.
            // FIXME: this is a bit of a hack but there isn't a formal way to convert a Directory
            // request into a Node request.
            let node_request = ServerEnd::new(dir.into_channel());
            let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
            let merkle = BlobId::from(meta_far_blob_id.merkle_root).to_string();
            pkgfs.open(flags, 0, &merkle, node_request)?;
            Ok(())
        }
    }

    struct ResolveTest {
        amber_proxy: AmberProxy,
        cache_proxy: PackageCacheProxy,
        pkgfs: Rc<TempDir>,
    }

    impl ResolveTest {
        fn new(amber_chan: Channel, cache_chan: Channel) -> ResolveTest {
            let amber_proxy = AmberProxy::new(fasync::Channel::from_channel(amber_chan).unwrap());
            let cache_proxy =
                PackageCacheProxy::new(fasync::Channel::from_channel(cache_chan).unwrap());

            let pkgfs = Rc::new(TempDir::new().expect("failed to create tmp dir"));

            ResolveTest { amber_proxy, cache_proxy, pkgfs }
        }

        fn start_services(&self, amber_s: Channel, cache_s: Channel, packages: PackageMap) {
            {
                let pkgfs = self.pkgfs.clone();
                fasync::spawn_local(
                    async move {
                        let mut amber = MockAmber::new(packages, pkgfs);
                        await!(amber.run(fasync::Channel::from_channel(amber_s).unwrap()))
                            .expect("amber failed");
                    },
                );
            }
            {
                let pkgfs = self.pkgfs.clone();
                fasync::spawn_local(
                    async move {
                        let cache = MockPackageCache::new(pkgfs);
                        await!(cache.run(fasync::Channel::from_channel(cache_s).unwrap()))
                            .expect("package cache failed");
                    },
                );
            }
        }

        fn check_dir(&self, dir_path: &Path, want_files: &Vec<String>) {
            let mut files: Vec<String> = fs::read_dir(&dir_path)
                .expect("could not read dir")
                .into_iter()
                .map(|entry| {
                    entry
                        .expect("get directory entry")
                        .file_name()
                        .to_str()
                        .expect("valid utf8")
                        .into()
                })
                .collect();
            files.sort_unstable();
            assert_eq!(&files, want_files);
        }

        async fn check_dir_async<'a>(
            &'a self,
            dir: &'a DirectoryProxy,
            want_files: &'a Vec<String>,
        ) {
            let entries = await!(files_async::readdir(dir)).expect("could not read dir");
            let mut files: Vec<_> = entries.into_iter().map(|f| f.name).collect();
            files.sort_unstable();
            assert_eq!(&files, want_files);
        }

        async fn check_amber_update<'a>(
            &'a self,
            name: &'a str,
            variant: Option<&'a str>,
            merkle: Option<&'a str>,
            expected_res: Result<String, Status>,
        ) {
            let chan = await!(self.amber_proxy.get_update_complete(name, variant, merkle))
                .expect("error communicating with amber");
            let expected_res = expected_res.map(|r| r.parse().expect("could not parse blob"));
            let res = await!(wait_for_update_to_complete(chan));
            assert_eq!(res, expected_res);
        }

        async fn run_resolve<'a>(
            &'a self,
            uri: &'a str,
            expected_res: Result<Vec<String>, Status>,
        ) {
            let selectors = vec![];
            let update_policy = UpdatePolicy { fetch_if_absent: true, allow_old_versions: false };
            let (package_dir_c, package_dir_s) = Channel::create().unwrap();
            let res = await!(resolve(
                &self.amber_proxy,
                &self.cache_proxy,
                uri.to_string(),
                selectors,
                update_policy,
                ServerEnd::new(package_dir_s),
            ));
            if res.is_ok() {
                let expected_files = expected_res.as_ref().unwrap();
                let dir_proxy =
                    DirectoryProxy::new(fasync::Channel::from_channel(package_dir_c).unwrap());
                await!(self.check_dir_async(&dir_proxy, expected_files));
            }
            assert_eq!(res, expected_res.map(|_s| ()), "unexpected result for {}", uri);
        }
    }

    fn gen_merkle(c: char) -> String {
        (0..64).map(|_| c).collect()
    }

    fn gen_merkle_file(c: char) -> String {
        format!("{}_file", gen_merkle(c))
    }

    #[test]
    fn test_mock_amber() {
        let mut executor = fasync::Executor::new().unwrap();
        let (amber_c, amber_s) = Channel::create().unwrap();
        let (cache_c, cache_s) = Channel::create().unwrap();
        let test = ResolveTest::new(amber_c, cache_c);
        let mut packages: PackageMap = HashMap::new();
        packages.insert(PackageId::new_with_name("foo"), gen_merkle('a'));
        packages.insert(PackageId::new_with_variant("bar", "stable"), gen_merkle('b'));
        packages
            .insert(PackageId::new_with_merkle("bar", "stable", &gen_merkle('c')), gen_merkle('c'));
        packages.insert(PackageId::new_with_name("buz"), gen_merkle('d'));
        test.start_services(amber_s, cache_s, packages);
        executor.run_singlethreaded(
            async move {
                // Name
                await!(test.check_amber_update("foo", None, None, Ok(gen_merkle('a'))));
                // Name and variant
                await!(test.check_amber_update("bar", Some("stable"), None, Ok(gen_merkle('b'))));
                // Name, variant, and merkle
                let merkle = gen_merkle('c');
                await!(test.check_amber_update(
                    "bar",
                    Some("stable"),
                    Some(&merkle),
                    Ok(gen_merkle('c'))
                ));
                // Nonexistent package
                await!(test.check_amber_update("nonexistent", None, None, Err(Status::INTERNAL)));
                // no merkle('d') since we didn't ask to update "buz".
                test.check_dir(
                    test.pkgfs.path(),
                    &vec![gen_merkle('a'), gen_merkle('b'), gen_merkle('c')],
                );
            },
        );
    }

    #[test]
    fn test_resolve_package() {
        let mut executor = fasync::Executor::new().unwrap();
        let (amber_c, amber_s) = Channel::create().unwrap();
        let (cache_c, cache_s) = Channel::create().unwrap();
        let test = ResolveTest::new(amber_c, cache_c);
        let mut packages: PackageMap = HashMap::new();
        packages.insert(PackageId::new_with_name("foo"), gen_merkle('a'));
        packages.insert(PackageId::new_with_variant("bar", "stable"), gen_merkle('b'));
        packages
            .insert(PackageId::new_with_merkle("bar", "stable", &gen_merkle('c')), gen_merkle('c'));
        test.start_services(amber_s, cache_s, packages);
        executor.run_singlethreaded(
            async move {
                // Package name
                await!(test
                    .run_resolve("fuchsia-pkg://fuchsia.com/foo", Ok(vec![gen_merkle_file('a')]),));
                // Package name and variant
                await!(test.run_resolve(
                    "fuchsia-pkg://fuchsia.com/bar/stable",
                    Ok(vec![gen_merkle_file('b')]),
                ));
                // Package name, variant, and merkle
                let url = format!("fuchsia-pkg://fuchsia.com/bar/stable/{}", gen_merkle('c'));
                await!(test.run_resolve(&url, Ok(vec![gen_merkle_file('c')],)));
                // Package resource
                await!(test.run_resolve(
                    "fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx",
                    Ok(vec![gen_merkle_file('a')]),
                ));
            },
        );
    }

    #[test]
    fn test_resolve_package_error() {
        let mut executor = fasync::Executor::new().unwrap();
        let (amber_c, amber_s) = Channel::create().unwrap();
        let (cache_c, cache_s) = Channel::create().unwrap();
        let test = ResolveTest::new(amber_c, cache_c);
        let mut packages: PackageMap = HashMap::new();
        packages.insert(PackageId::new_with_variant("foo", "stable"), gen_merkle('a'));
        test.start_services(amber_s, cache_s, packages);
        executor.run_singlethreaded(
            async move {
                // Missing package
                await!(
                    test.run_resolve("fuchsia-pkg://fuchsia.com/foo/beta", Err(Status::INTERNAL),)
                );

                // Bad package URI
                await!(
                    test.run_resolve("fuchsia-pkg://fuchsia.com/foo!", Err(Status::INVALID_ARGS),)
                );

                // No package name
                await!(test.run_resolve("fuchsia-pkg://fuchsia.com", Err(Status::INVALID_ARGS),));
            },
        );
    }
}
