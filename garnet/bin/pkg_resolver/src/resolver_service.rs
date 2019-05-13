// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::rewrite_manager::RewriteManager,
    failure::{Error, ResultExt},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_amber::{self, ControlMarker as AmberMarker, ControlProxy as AmberProxy},
    fidl_fuchsia_io::{self, DirectoryMarker},
    fidl_fuchsia_pkg::{
        PackageCacheProxy, PackageResolverRequest, PackageResolverRequestStream, UpdatePolicy,
    },
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_uri::pkg_uri::PkgUri,
    fuchsia_zircon::{Channel, MessageBuf, Signals, Status},
    futures::prelude::*,
    lazy_static::lazy_static,
    log::{info, warn},
    parking_lot::RwLock,
    regex::Regex,
    std::sync::Arc,
};

lazy_static! {
    // The error amber returns if it could not find the merkle for this package.
    static ref PACKAGE_NOT_FOUND_RE: Regex =
        Regex::new("^merkle not found for package ").unwrap();

    // The error amber returns if it could resolve a merkle for this package, but it couldn't
    // download the package.
    static ref UNAVAILABLE_RE: Regex =
        Regex::new("^not found in \\d+ active sources").unwrap();
}

pub async fn run_resolver_service(
    rewrites: Arc<RwLock<RewriteManager>>,
    mut amber: AmberProxy,
    cache: PackageCacheProxy,
    mut stream: PackageResolverRequestStream,
) -> Result<(), Error> {
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
            info!("Reconnecting to amber");
            amber = connect_to_service::<AmberMarker>().context("error connecting to amber")?;
            should_reconnect = false;
        }

        let status =
            await!(resolve(&rewrites, &amber, &cache, package_uri, selectors, update_policy, dir));

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
    rewrites: &'a Arc<RwLock<RewriteManager>>,
    amber: &'a AmberProxy,
    cache: &'a PackageCacheProxy,
    pkg_uri: String,
    selectors: Vec<String>,
    _update_policy: UpdatePolicy,
    dir_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status> {
    let uri = PkgUri::parse(&pkg_uri).map_err(|err| {
        fx_log_err!("failed to parse package uri {:?}: {}", pkg_uri, err);
        Err(Status::INVALID_ARGS)
    })?;
    let was_fuchsia_host = uri.host() == "fuchsia.com";
    let uri = rewrites.read().rewrite(uri);

    // FIXME: at the moment only the fuchsia.com host is supported.
    if !was_fuchsia_host && uri.host() != "fuchsia.com" {
        fx_log_err!("package uri's host is currently unsupported: {}", uri);
        return Err(Status::INVALID_ARGS);
    }

    // While the fuchsia-pkg:// spec doesn't require a package name, we do.
    let name = uri.name().ok_or_else(|| {
        fx_log_err!("package uri is missing a package name: {}", uri);
        Err(Status::INVALID_ARGS)
    })?;

    // While the fuchsia-pkg:// spec allows resource paths, the package resolver should not be
    // given one.
    if uri.resource().is_some() {
        fx_log_err!("package uri should not contain a resource name: {}", uri);
        return Err(Status::INVALID_ARGS);
    }

    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("resolve does not support selectors yet");
    }

    // FIXME: use the package cache to fetch the package instead of amber.

    // Ask amber to cache the package.
    let chan = await!(amber.get_update_complete(&name, uri.variant(), uri.package_hash()))
        .map_err(|err| {
            fx_log_err!("error communicating with amber: {:?}", err);
            Status::INTERNAL
        })?;

    let merkle = await!(wait_for_update_to_complete(chan, &uri)).map_err(|err| {
        fx_log_err!("error when waiting for amber to complete: {:?}", err);
        err
    })?;

    fx_log_info!(
        "resolved {} as {} with the selectors {:?} to {}",
        pkg_uri,
        uri,
        selectors,
        merkle
    );

    await!(cache.open(&mut merkle.into(), &mut selectors.iter().map(|s| s.as_str()), dir_request))
        .map_err(|err| {
            fx_log_err!("error opening {}: {:?}", merkle, err);
            Status::INTERNAL
        })?;

    Ok(())
}

async fn wait_for_update_to_complete(chan: Channel, uri: &PkgUri) -> Result<BlobId, Status> {
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

            if PACKAGE_NOT_FOUND_RE.is_match(&msg) {
                fx_log_info!("package {} was not found: {}", uri, msg);
                return Err(Status::NOT_FOUND);
            }

            if UNAVAILABLE_RE.is_match(&msg) {
                fx_log_info!("package {} is currently unavailable: {}", uri, msg);
                return Err(Status::UNAVAILABLE);
            }

            fx_log_err!("error installing package {}: {}", uri, msg);

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
    use crate::rewrite_manager::{tests::make_rule_config, RewriteManagerBuilder};
    use failure::Error;
    use fidl::endpoints::{self, ServerEnd};
    use fidl_fuchsia_amber::ControlRequest;
    use fidl_fuchsia_io::DirectoryProxy;
    use fidl_fuchsia_pkg::{self, PackageCacheProxy, PackageCacheRequest, UpdatePolicy};
    use files_async;
    use fuchsia_async as fasync;
    use fuchsia_zircon::{Channel, Peered, Signals, Status};
    use std::cell::RefCell;
    use std::collections::HashMap;
    use std::fs::{self, File};
    use std::io;
    use std::path::Path;
    use std::rc::Rc;
    use std::str;
    use tempfile::TempDir;

    struct Package {
        name: String,
        variant: String,
        merkle: String,
        kind: PackageKind,
    }

    enum PackageKind {
        Ok,
        Error(String),
    }

    impl Package {
        fn new(name: &str, variant: &str, merkle: &str, kind: PackageKind) -> Self {
            Self {
                name: name.to_string(),
                variant: variant.to_string(),
                merkle: merkle.to_string(),
                kind: kind,
            }
        }
    }

    struct MockAmber {
        packages: HashMap<(String, String), Package>,
        pkgfs: Rc<TempDir>,
        channels: Vec<Channel>,
    }

    impl MockAmber {
        fn new(packages: Vec<Package>, pkgfs: Rc<TempDir>) -> MockAmber {
            let mut package_map = HashMap::new();
            for package in packages {
                package_map.insert((package.name.clone(), package.variant.clone()), package);
            }
            MockAmber { packages: package_map, pkgfs, channels: vec![] }
        }

        fn get_update_complete(&mut self, req: ControlRequest) -> Result<(), Error> {
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

    struct MockPackageCache {
        pkgfs: DirectoryProxy,
    }

    impl MockPackageCache {
        fn new(pkgfs: Rc<TempDir>) -> Result<MockPackageCache, Error> {
            let f = File::open(pkgfs.path())?;
            let pkgfs =
                DirectoryProxy::new(fasync::Channel::from_channel(fdio::clone_channel(&f)?)?);
            Ok(MockPackageCache { pkgfs })
        }

        fn open(&self, req: PackageCacheRequest) {
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

    struct ResolveTest {
        rewrite_manager: Arc<RwLock<RewriteManager>>,
        amber_proxy: AmberProxy,
        cache_proxy: PackageCacheProxy,
        pkgfs: Rc<TempDir>,
    }

    impl ResolveTest {
        fn new(
            rewrite_manager: Arc<RwLock<RewriteManager>>,
            packages: Vec<Package>,
        ) -> ResolveTest {
            let pkgfs = Rc::new(TempDir::new().expect("failed to create tmp dir"));
            let amber = Rc::new(RefCell::new(MockAmber::new(packages, pkgfs.clone())));
            let amber_proxy: AmberProxy = endpoints::spawn_local_stream_handler(move |req| {
                let amber = amber.clone();
                async move {
                    amber.borrow_mut().get_update_complete(req).expect("amber failed");
                }
            })
            .expect("failed to spawn handler");
            let cache =
                Rc::new(MockPackageCache::new(pkgfs.clone()).expect("failed to create cache"));
            let cache_proxy: PackageCacheProxy =
                endpoints::spawn_local_stream_handler(move |req| {
                    let cache = cache.clone();
                    async move {
                        cache.open(req);
                    }
                })
                .expect("failed to spawn handler");
            ResolveTest { rewrite_manager, amber_proxy, cache_proxy, pkgfs }
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

            let path = match variant {
                None => format!("/{}", name),
                Some(variant) => format!("/{}/{}", name, variant),
            };

            let uri =
                PkgUri::new_package("fuchsia.com".to_string(), path, merkle.map(|s| s.to_string()))
                    .unwrap();

            let res = await!(wait_for_update_to_complete(chan, &uri));
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
                &self.rewrite_manager,
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mock_amber() {
        let dynamic_rule_config = make_rule_config(vec![]);
        let rewrite_manager = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(&dynamic_rule_config).unwrap().build(),
        ));
        let packages = vec![
            Package::new("foo", "0", &gen_merkle('a'), PackageKind::Ok),
            Package::new("bar", "stable", &gen_merkle('b'), PackageKind::Ok),
            Package::new("baz", "stable", &gen_merkle('c'), PackageKind::Ok),
            Package::new("buz", "0", &gen_merkle('c'), PackageKind::Ok),
        ];
        let test = ResolveTest::new(rewrite_manager, packages);

        // Name
        await!(test.check_amber_update("foo", None, None, Ok(gen_merkle('a'))));

        // Name and variant
        await!(test.check_amber_update("bar", Some("stable"), None, Ok(gen_merkle('b'))));

        // Name, variant, and merkle
        let merkle = gen_merkle('c');
        await!(test.check_amber_update("baz", Some("stable"), Some(&merkle), Ok(gen_merkle('c'))));

        // Nonexistent package
        await!(test.check_amber_update("nonexistent", None, None, Err(Status::NOT_FOUND)));

        // no merkle('d') since we didn't ask to update "buz".
        test.check_dir(test.pkgfs.path(), &vec![gen_merkle('a'), gen_merkle('b'), gen_merkle('c')]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_resolve_package() {
        let dynamic_rule_config = make_rule_config(vec![]);
        let rewrite_manager = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(&dynamic_rule_config).unwrap().build(),
        ));
        let packages = vec![
            Package::new("foo", "0", &gen_merkle('a'), PackageKind::Ok),
            Package::new("bar", "stable", &gen_merkle('b'), PackageKind::Ok),
        ];
        let test = ResolveTest::new(rewrite_manager, packages);

        // Package name
        await!(test.run_resolve("fuchsia-pkg://fuchsia.com/foo", Ok(vec![gen_merkle_file('a')]),));

        // Package name and variant
        await!(test
            .run_resolve("fuchsia-pkg://fuchsia.com/bar/stable", Ok(vec![gen_merkle_file('b')]),));

        // Package name, variant, and merkle
        let url = format!("fuchsia-pkg://fuchsia.com/bar/stable?hash={}", gen_merkle('b'));
        await!(test.run_resolve(&url, Ok(vec![gen_merkle_file('b')],)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_resolve_package_error() {
        let dynamic_rule_config = make_rule_config(vec![]);
        let rewrite_manager = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(&dynamic_rule_config).unwrap().build(),
        ));
        let packages = vec![
            Package::new("foo", "stable", &gen_merkle('a'), PackageKind::Ok),
            Package::new(
                "unavailable",
                "0",
                &gen_merkle('a'),
                PackageKind::Error("not found in 1 active sources. last error: ".to_string()),
            ),
        ];
        let test = ResolveTest::new(rewrite_manager, packages);

        // Missing package
        await!(test.run_resolve("fuchsia-pkg://fuchsia.com/foo/beta", Err(Status::NOT_FOUND)));

        // Unavailable package
        await!(
            test.run_resolve("fuchsia-pkg://fuchsia.com/unavailable/0", Err(Status::UNAVAILABLE))
        );

        // Bad package URI
        await!(test.run_resolve("fuchsia-pkg://fuchsia.com/foo!", Err(Status::INVALID_ARGS)));

        // No package name
        await!(test.run_resolve("fuchsia-pkg://fuchsia.com", Err(Status::INVALID_ARGS)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_resolve_package_unknown_host() {
        let rules = vec![fuchsia_uri_rewrite::Rule::new(
            "example.com".to_owned(),
            "fuchsia.com".to_owned(),
            "/foo/".to_owned(),
            "/foo/".to_owned(),
        )
        .unwrap()];
        let dynamic_rule_config = make_rule_config(vec![]);
        let rewrite_manager = Arc::new(RwLock::new(
            RewriteManagerBuilder::new(&dynamic_rule_config).unwrap().static_rules(rules).build(),
        ));
        let packages = vec![
            Package::new("foo", "0", &gen_merkle('a'), PackageKind::Ok),
            Package::new("bar", "stable", &gen_merkle('b'), PackageKind::Ok),
        ];
        let test = ResolveTest::new(rewrite_manager, packages);

        await!(test.run_resolve("fuchsia-pkg://example.com/foo/0", Ok(vec![gen_merkle_file('a')]),));
        await!(test.run_resolve("fuchsia-pkg://fuchsia.com/foo/0", Ok(vec![gen_merkle_file('a')]),));
        await!(test.run_resolve("fuchsia-pkg://example.com/bar/stable", Err(Status::INVALID_ARGS)));
        await!(test
            .run_resolve("fuchsia-pkg://fuchsia.com/bar/stable", Ok(vec![gen_merkle_file('b')]),));
    }
}
