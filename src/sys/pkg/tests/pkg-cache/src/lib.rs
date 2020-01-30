// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{PackageCacheMarker, PackageCacheProxy},
    fidl_fuchsia_pkg_ext::BlobId,
    fidl_fuchsia_space::{ManagerMarker as SpaceManagerMarker, ManagerProxy as SpaceManagerProxy},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_inspect::reader::NodeHierarchy,
    fuchsia_pkg_testing::get_inspect_hierarchy,
    fuchsia_zircon as zx,
    futures::prelude::*,
    pkgfs_ramdisk::PkgfsRamdisk,
};

mod inspect;
mod space;

trait PkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;
}

impl PkgFs for PkgfsRamdisk {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        PkgfsRamdisk::root_dir_handle(self)
    }
}

struct Proxies {
    space_manager: SpaceManagerProxy,
    package_cache: PackageCacheProxy,
}

struct TestEnv<P = PkgfsRamdisk> {
    _env: NestedEnvironment,
    pkgfs: P,
    proxies: Proxies,
    pkg_cache: App,
    nested_environment_label: String,
}

impl TestEnv<PkgfsRamdisk> {
    // workaround for fxb/38162
    async fn stop(self) {
        // Tear down the environment in reverse order, ending with the storage.
        drop(self.proxies);
        drop(self.pkg_cache);
        drop(self._env);
        self.pkgfs.stop().await.unwrap();
    }
}

impl<P: PkgFs> TestEnv<P> {
    fn new(pkgfs: P) -> Self {
        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_tracing_provider::RegistryMarker, _>();

        let pkg_cache = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-cache-integration-tests#meta/pkg-cache-without-pkgfs.cmx".to_string(),
        )
            .add_handle_to_namespace("/pkgfs".to_owned(), pkgfs.root_dir_handle().unwrap().into());

        let nested_environment_label = Self::make_nested_environment_label();
        let env = fs
            .create_nested_environment(&nested_environment_label)
            .expect("nested environment to create successfully");

        fasync::spawn(fs.collect());

        let pkg_cache = pkg_cache.spawn(env.launcher()).expect("pkg_cache to launch");

        let proxies = Proxies {
            space_manager: pkg_cache
                .connect_to_service::<SpaceManagerMarker>()
                .expect("connect to space manager"),
            package_cache: pkg_cache.connect_to_service::<PackageCacheMarker>().unwrap(),
        };

        Self { _env: env, pkgfs, proxies, pkg_cache, nested_environment_label }
    }

    async fn inspect_hierarchy(&self) -> NodeHierarchy {
        get_inspect_hierarchy(&self.nested_environment_label, "pkg-cache-without-pkgfs.cmx").await
    }

    fn make_nested_environment_label() -> String {
        let mut salt = [0; 4];
        zx::cprng_draw(&mut salt[..]).expect("zx_cprng_draw does not fail");
        format!("pkg-cache-env_{}", hex::encode(&salt))
    }

    async fn block_until_started(&self) {
        let (_, server_end) = fidl::endpoints::create_endpoints().unwrap();
        self.proxies
            .package_cache
            .open(
                &mut "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse::<BlobId>()
                    .unwrap()
                    .into(),
                &mut vec![].into_iter(),
                server_end,
            )
            .await
            .expect("fidl should succeed, but result of open doesn't matter");
    }
}
