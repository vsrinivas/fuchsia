// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![cfg(test)]

use {
    failure::Error,
    fidl_fuchsia_amber::ControlMarker as AmberMarker,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_pkg::{
        ExperimentToggle as Experiment, PackageCacheMarker, PackageResolverAdminMarker,
        PackageResolverAdminProxy, PackageResolverMarker, PackageResolverProxy,
        RepositoryManagerMarker, RepositoryManagerProxy, UpdatePolicy,
    },
    fidl_fuchsia_sys::LauncherProxy,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_pkg_testing::{pkgfs::TestPkgFs, PackageBuilder, RepositoryBuilder},
    fuchsia_zircon::Status,
    futures::prelude::*,
    matches::assert_matches,
};

struct Proxies {
    resolver_admin: PackageResolverAdminProxy,
    resolver: PackageResolverProxy,
    repo_manager: RepositoryManagerProxy,
}

struct TestEnv {
    _amber: App,
    _pkg_cache: App,
    _pkg_resolver: App,
    pkgfs: TestPkgFs,
    env: NestedEnvironment,
    proxies: Proxies,
}

impl TestEnv {
    fn new() -> Self {
        let pkgfs = TestPkgFs::start(None).expect("pkgfs to start");

        let mut amber =
            AppBuilder::new("fuchsia-pkg://fuchsia.com/pkg-resolver-tests#meta/amber.cmx")
                .add_dir_to_namespace(
                    "/pkgfs".to_owned(),
                    pkgfs.root_dir_file().expect("pkgfs dir to open"),
                )
                .expect("/pkgfs to mount");

        let mut pkg_cache = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-resolver-tests#meta/pkg_cache.cmx".to_owned(),
        )
        .add_dir_to_namespace(
            "/pkgfs".to_owned(),
            pkgfs.root_dir_file().expect("pkgfs dir to open"),
        )
        .expect("/pkgfs to mount");

        let mut pkg_resolver = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-resolver-tests#meta/pkg_resolver.cmx".to_owned(),
        )
        .add_dir_to_namespace(
            "/pkgfs".to_owned(),
            pkgfs.root_dir_file().expect("pkgfs dir to open"),
        )
        .expect("/pkgfs to mount");

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_net::SocketProviderMarker, _>()
            .add_proxy_service::<fidl_fuchsia_net::NameLookupMarker, _>()
            .add_proxy_service::<fidl_fuchsia_posix_socket::ProviderMarker, _>()
            .add_proxy_service_to::<AmberMarker, _>(amber.directory_request().unwrap().clone())
            .add_proxy_service_to::<PackageCacheMarker, _>(
                pkg_cache.directory_request().unwrap().clone(),
            )
            .add_proxy_service_to::<RepositoryManagerMarker, _>(
                pkg_resolver.directory_request().unwrap().clone(),
            )
            .add_proxy_service_to::<PackageResolverAdminMarker, _>(
                pkg_resolver.directory_request().unwrap().clone(),
            )
            .add_proxy_service_to::<PackageResolverMarker, _>(
                pkg_resolver.directory_request().unwrap().clone(),
            );

        let env = fs
            .create_salted_nested_environment("pkg-resolver-env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let amber = amber.spawn(env.launcher()).expect("amber to launch");
        let pkg_cache = pkg_cache.spawn(env.launcher()).expect("package cache to launch");
        let pkg_resolver = pkg_resolver.spawn(env.launcher()).expect("package resolver to launch");

        let resolver_proxy =
            env.connect_to_service::<PackageResolverMarker>().expect("connect to package resolver");
        let resolver_admin_proxy = env
            .connect_to_service::<PackageResolverAdminMarker>()
            .expect("connect to package resolver admin");
        let repo_manager_proxy = env
            .connect_to_service::<RepositoryManagerMarker>()
            .expect("connect to repository manager");

        Self {
            _amber: amber,
            _pkg_cache: pkg_cache,
            _pkg_resolver: pkg_resolver,
            env,
            pkgfs,
            proxies: Proxies {
                resolver: resolver_proxy,
                resolver_admin: resolver_admin_proxy,
                repo_manager: repo_manager_proxy,
            },
        }
    }

    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    async fn resolve_package(&self, url: &str) -> Result<DirectoryProxy, Status> {
        let (package, package_server_end) = fidl::endpoints::create_proxy().unwrap();
        let selectors: Vec<&str> = vec![];
        let status = self
            .proxies
            .resolver
            .resolve(
                url,
                &mut selectors.into_iter(),
                &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
                package_server_end,
            )
            .await
            .expect("package resolve fidl call");
        Status::ok(status)?;
        Ok(package)
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_package_resolution() -> Result<(), Error> {
    let env = TestEnv::new();

    let pkg = PackageBuilder::new("rolldice")
        .add_resource_at("bin/rolldice", "#!/boot/bin/sh\necho 4\n".as_bytes())?
        .add_resource_at(
            "meta/rolldice.cmx",
            r#"{"program":{"binary":"bin/rolldice"}}"#.as_bytes(),
        )?
        .add_resource_at("data/duplicate_a", "same contents".as_bytes())?
        .add_resource_at("data/duplicate_b", "same contents".as_bytes())?
        .build()
        .await?;
    let repo = RepositoryBuilder::new().add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.proxies.repo_manager.add(repo_config.into()).await?;

    let package = env
        .resolve_package("fuchsia-pkg://test/rolldice")
        .await
        .expect("package to resolve without error");

    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.expect("correct package contents");

    // All blobs in the repository should now be present in blobfs.
    assert_eq!(env.pkgfs.blobfs().list_blobs().unwrap(), repo.list_blobs().unwrap());

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_experiment_fails() -> Result<(), Error> {
    let env = TestEnv::new();

    let repo = RepositoryBuilder::new()
        .add_package(
            PackageBuilder::new("rolldice")
                .add_resource_at("bin/rolldice", "#!/boot/bin/sh\necho 4\n".as_bytes())?
                .build()
                .await?,
        )
        .build()
        .await?;
    let served_repository = repo.serve(env.launcher()).await?;

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.proxies.repo_manager.add(repo_config.into()).await?;

    env.proxies
        .resolver_admin
        .set_experiment_state(Experiment::DownloadBlob, true)
        .await
        .expect("experiment state to toggle");

    assert_matches!(env.resolve_package("fuchsia-pkg://test/rolldice").await, Err(_));

    Ok(())
}
