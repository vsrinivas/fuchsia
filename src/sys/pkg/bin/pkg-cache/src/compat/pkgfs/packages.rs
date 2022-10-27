// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{base_packages::BasePackages, index::PackageIndex},
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_hash::Hash,
    fuchsia_pkg::{PackageName, PackageVariant},
    fuchsia_zircon as zx,
    std::{
        collections::{BTreeMap, HashMap},
        str::FromStr,
        sync::Arc,
    },
    system_image::NonStaticAllowList,
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::io1::DerivedConnection,
            dirents_sink,
            entry::{DirectoryEntry, EntryInfo},
            entry_container::{Directory, DirectoryWatcher},
            immutable::connection::io1::ImmutableConnection,
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path,
    },
};

mod variants;
use variants::PkgfsPackagesVariants;

#[derive(Debug)]
pub struct PkgfsPackages {
    base_packages: Arc<BasePackages>,
    non_base_packages: Arc<async_lock::RwLock<PackageIndex>>,
    non_static_allow_list: Arc<NonStaticAllowList>,
    blobfs: blobfs::Client,
}

impl PkgfsPackages {
    pub fn new(
        base_packages: Arc<BasePackages>,
        non_base_packages: Arc<async_lock::RwLock<PackageIndex>>,
        non_static_allow_list: Arc<NonStaticAllowList>,
        blobfs: blobfs::Client,
    ) -> Self {
        Self { base_packages, non_base_packages, non_static_allow_list, blobfs }
    }

    async fn packages(&self) -> HashMap<PackageName, HashMap<PackageVariant, Hash>> {
        let mut res: HashMap<PackageName, HashMap<PackageVariant, Hash>> = HashMap::new();

        // First populate with base packages.
        for (path, hash) in self.base_packages.paths_and_hashes() {
            let name = path.name().to_owned();
            let variant = path.variant().to_owned();

            res.entry(name).or_default().insert(variant, *hash);
        }

        // Then fill in allowed dynamic packages, which may not override existing base packages.
        let active_packages = self.non_base_packages.read().await.active_packages();
        for (path, hash) in active_packages {
            if !self.non_static_allow_list.allows(path.name()) {
                continue;
            }

            let (name, variant) = path.into_name_and_variant();

            res.entry(name).or_default().entry(variant).or_insert(hash);
        }

        res
    }

    async fn package_variants(&self, name: &PackageName) -> Option<HashMap<PackageVariant, Hash>> {
        self.packages().await.remove(name)
    }

    async fn directory_entries(&self) -> BTreeMap<String, super::DirentType> {
        self.packages()
            .await
            .into_keys()
            .map(|k| (k.into(), super::DirentType::Directory))
            .collect()
    }
}

impl DirectoryEntry for PkgfsPackages {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        mut path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        let flags = flags.difference(fio::OpenFlags::POSIX_WRITABLE);

        // This directory and all child nodes are read-only
        if flags.intersects(
            fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE
                | fio::OpenFlags::CREATE_IF_ABSENT
                | fio::OpenFlags::TRUNCATE
                | fio::OpenFlags::APPEND,
        ) {
            return send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
        }

        scope.clone().spawn(async move {
            match path.next().map(PackageName::from_str) {
                None => ImmutableConnection::create_connection(scope, self, flags, server_end),
                Some(Ok(package_name)) => match self.package_variants(&package_name).await {
                    Some(variants) => {
                        Arc::new(PkgfsPackagesVariants::new(variants, self.blobfs.clone()))
                            .open(scope, flags, mode, path, server_end)
                    }
                    None => send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND),
                },
                Some(Err(_)) => {
                    // Names that are not valid package names can't exist in this directory.
                    send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND)
                }
            }
        })
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
    }
}

#[async_trait]
impl Directory for PkgfsPackages {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        sink: Box<(dyn dirents_sink::Sink + 'static)>,
    ) -> Result<(TraversalPosition, Box<(dyn dirents_sink::Sealed + 'static)>), zx::Status> {
        // If directory contents changes in between a client making paginated
        // fuchsia.io/Directory.ReadDirents calls, the client may not see a consistent snapshot
        // of the directory contents.
        super::read_dirents(&self.directory_entries().await, pos, sink).await
    }

    fn register_watcher(
        self: Arc<Self>,
        _: ExecutionScope,
        _: fio::WatchMask,
        _: DirectoryWatcher,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so this is a no-op.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_DIRECTORY,
            id: 1,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{compat::pkgfs::testing::FakeSink, index::register_dynamic_package},
        assert_matches::assert_matches,
        fuchsia_pkg::PackagePath,
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        maplit::{convert_args, hashmap},
        std::collections::HashSet,
    };

    impl PkgfsPackages {
        pub fn new_test(
            base_packages: impl IntoIterator<Item = (PackagePath, Hash)>,
            non_static_allow_list: NonStaticAllowList,
        ) -> (Arc<Self>, Arc<async_lock::RwLock<PackageIndex>>) {
            let (blobfs, _) = blobfs::Client::new_mock();
            let index = Arc::new(async_lock::RwLock::new(PackageIndex::new_test()));

            (
                Arc::new(PkgfsPackages::new(
                    Arc::new(BasePackages::new_test_only(
                        // PkgfsPackages only uses the path-hash mapping, so tests do not need to
                        // populate the blob hashes.
                        HashSet::new(),
                        base_packages,
                    )),
                    Arc::clone(&index),
                    Arc::new(non_static_allow_list),
                    blobfs,
                )),
                index,
            )
        }

        fn proxy(self: &Arc<Self>, flags: fio::OpenFlags) -> fio::DirectoryProxy {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

            vfs::directory::entry::DirectoryEntry::open(
                Arc::clone(self),
                ExecutionScope::new(),
                flags,
                0,
                Path::dot(),
                server_end.into_channel().into(),
            );

            proxy
        }
    }

    macro_rules! package_name_hashmap {
        ($($inner:tt)*) => {
            convert_args!(
                keys = |s| PackageName::from_str(s).unwrap(),
                hashmap!($($inner)*)
            )
        };
    }

    macro_rules! package_variant_hashmap {
        ($($inner:tt)*) => {
            convert_args!(
                keys = |s| PackageVariant::from_str(s).unwrap(),
                hashmap!($($inner)*)
            )
        };
    }

    fn non_static_allow_list(names: &[&str]) -> NonStaticAllowList {
        NonStaticAllowList::parse(names.join("\n").as_bytes()).unwrap()
    }

    fn hash(n: u8) -> Hash {
        Hash::from([n; 32])
    }

    fn path(name: &str, variant: &str) -> PackagePath {
        PackagePath::from_name_and_variant(name.parse().unwrap(), variant.parse().unwrap())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn minimal_lifecycle() {
        let (pkgfs_packages, _package_index) =
            PkgfsPackages::new_test([], non_static_allow_list(&[]));

        drop(pkgfs_packages);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn packages_listing_unions_indices() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            [(path("static", "0"), hash(0))],
            non_static_allow_list(&["dynamic"]),
        );

        register_dynamic_package(&package_index, path("dynamic", "0"), hash(1)).await;
        register_dynamic_package(&package_index, path("dynamic", "1"), hash(2)).await;

        assert_eq!(
            pkgfs_packages.packages().await,
            package_name_hashmap!(
                "static" => package_variant_hashmap!{
                    "0" => hash(0),
                },
                "dynamic" => package_variant_hashmap!{
                    "0" => hash(1),
                    "1" => hash(2),
                },
            )
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn packages_listing_dynamic_does_not_override_static() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            [(path("base-package", "0"), hash(0))],
            non_static_allow_list(&["base-package"]),
        );

        register_dynamic_package(&package_index, path("base-package", "0"), hash(1)).await;
        register_dynamic_package(&package_index, path("base-package", "1"), hash(2)).await;

        assert_eq!(
            pkgfs_packages.packages().await,
            package_name_hashmap!(
                "base-package" => package_variant_hashmap!{
                    "0" => hash(0), // hash is still `0`, was not updated to `1`
                    "1" => hash(2), // registration of non-base variant succeeded
                },
            )
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_empty() {
        let (pkgfs_packages, _package_index) =
            PkgfsPackages::new_test([], non_static_allow_list(&[]));

        // Given adequate buffer space, the only entry is itself (".").
        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_packages,
            &TraversalPosition::Start,
            Box::new(FakeSink::new(100)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![(".".to_owned(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_enumerates_all_allowed_entries() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            [
                (path("allowed", "0"), hash(0)),
                (path("static", "0"), hash(1)),
                (path("static", "1"), hash(2)),
                (path("still", "static"), hash(3)),
            ],
            non_static_allow_list(&["allowed", "dynonly", "missing"]),
        );

        register_dynamic_package(&package_index, path("allowed", "dynamic-package"), hash(10))
            .await;
        register_dynamic_package(&package_index, path("static", "0"), hash(11)).await;
        register_dynamic_package(&package_index, path("dynamic", "0"), hash(12)).await;
        register_dynamic_package(&package_index, path("dynonly", "0"), hash(14)).await;

        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_packages,
            &TraversalPosition::Start,
            Box::new(FakeSink::new(100)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![
                (".".to_owned(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                (
                    "allowed".to_owned(),
                    EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
                ),
                (
                    "dynonly".to_owned(),
                    EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
                ),
                ("static".to_owned(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                ("still".to_owned(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_rejects_invalid_name() {
        let (pkgfs_packages, _package_index) =
            PkgfsPackages::new_test([], non_static_allow_list(&[]));

        let proxy = pkgfs_packages.proxy(fio::OpenFlags::RIGHT_READABLE);

        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &proxy,
                "invalidname-!@#$%^&*()+=",
                fio::OpenFlags::RIGHT_READABLE
            )
            .await,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_rejects_missing_package() {
        let (pkgfs_packages, _package_index) =
            PkgfsPackages::new_test([], non_static_allow_list(&[]));

        let proxy = pkgfs_packages.proxy(fio::OpenFlags::RIGHT_READABLE);

        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &proxy,
                "missing",
                fio::OpenFlags::RIGHT_READABLE
            )
            .await,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_opens_static_package_variants() {
        let (pkgfs_packages, _package_index) =
            PkgfsPackages::new_test([(path("static", "0"), hash(0))], non_static_allow_list(&[]));

        let proxy = pkgfs_packages.proxy(fio::OpenFlags::RIGHT_READABLE);

        assert_matches!(
            fuchsia_fs::directory::open_directory(&proxy, "static", fio::OpenFlags::RIGHT_READABLE)
                .await,
            Ok(_)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_opens_allowed_dynamic_variants() {
        let (pkgfs_packages, package_index) =
            PkgfsPackages::new_test([], non_static_allow_list(&["dynamic"]));

        let proxy = pkgfs_packages.proxy(fio::OpenFlags::RIGHT_READABLE);

        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &proxy,
                "dynamic",
                fio::OpenFlags::RIGHT_READABLE
            )
            .await,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );

        register_dynamic_package(&package_index, path("dynamic", "0"), hash(0)).await;

        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &proxy,
                "dynamic",
                fio::OpenFlags::RIGHT_READABLE
            )
            .await,
            Ok(_)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_opens_path_within_known_package_variant() {
        let package_index = Arc::new(async_lock::RwLock::new(PackageIndex::new_test()));
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        let pkgfs_packages = Arc::new(PkgfsPackages::new(
            Arc::new(BasePackages::new_test_only(HashSet::new(), [])),
            Arc::clone(&package_index),
            Arc::new(non_static_allow_list(&["dynamic"])),
            blobfs_client,
        ));

        let proxy = pkgfs_packages.proxy(fio::OpenFlags::RIGHT_READABLE);

        let package = PackageBuilder::new("dynamic")
            .add_resource_at("meta/message", &b"yes"[..])
            .build()
            .await
            .expect("created pkg");
        let (metafar_blob, _) = package.contents();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        register_dynamic_package(&package_index, path("dynamic", "0"), metafar_blob.merkle).await;

        let file = fuchsia_fs::directory::open_file(
            &proxy,
            "dynamic/0/meta/message",
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .unwrap();
        let message = fuchsia_fs::file::read_to_string(&file).await.unwrap();
        assert_eq!(message, "yes");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_unsets_posix_writable() {
        let (pkgfs_packages, _package_index) =
            PkgfsPackages::new_test([], non_static_allow_list(&[]));

        let proxy =
            pkgfs_packages.proxy(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::POSIX_WRITABLE);

        let (status, flags) = proxy.get_flags().await.unwrap();
        let () = zx::Status::ok(status).unwrap();
        assert_eq!(flags, fio::OpenFlags::RIGHT_READABLE);
    }
}
