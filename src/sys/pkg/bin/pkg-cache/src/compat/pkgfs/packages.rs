// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::index::PackageIndex,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MODE_TYPE_DIRECTORY,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_ADMIN, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_hash::Hash,
    fuchsia_pkg::{PackageName, PackageVariant},
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::{
        collections::{BTreeSet, HashMap},
        str::FromStr,
        sync::Arc,
    },
    system_image::NonStaticAllowList,
    vfs::{
        common::{rights_to_posix_mode_bits, send_on_open_with_error},
        directory::{
            connection::{io1::DerivedConnection, util::OpenDirectory},
            dirents_sink,
            entry::{DirectoryEntry, EntryInfo},
            entry_container::Directory,
            immutable::connection::io1::{ImmutableConnection, ImmutableConnectionClient},
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path,
    },
};

#[derive(Debug)]
struct PkgfsPackagesVariants {
    contents: HashMap<PackageVariant, Hash>,
    blobfs: blobfs::Client,
}
impl DirectoryEntry for PkgfsPackagesVariants {
    fn open(
        self: Arc<Self>,
        _scope: ExecutionScope,
        flags: u32,
        _mode: u32,
        _path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        // TODO(fxbug.dev/85268) Implement this, update tests here asserting open fails with
        // TIMED_OUT.
        send_on_open_with_error(flags, server_end, zx::Status::TIMED_OUT)
    }
    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
}

#[derive(Debug)]
pub struct PkgfsPackages {
    static_packages: system_image::StaticPackages,
    non_static_packages: Arc<Mutex<PackageIndex>>,
    non_static_allow_list: NonStaticAllowList,
    blobfs: blobfs::Client,
}

impl PkgfsPackages {
    // TODO use this
    #[allow(dead_code)]
    pub fn new(
        static_packages: system_image::StaticPackages,
        non_static_packages: Arc<Mutex<PackageIndex>>,
        non_static_allow_list: NonStaticAllowList,
        blobfs: blobfs::Client,
    ) -> Self {
        Self { static_packages, non_static_packages, non_static_allow_list, blobfs }
    }

    #[cfg(test)]
    fn proxy(self: &Arc<Self>) -> fidl_fuchsia_io::DirectoryProxy {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();

        let () = ImmutableConnection::create_connection(
            ExecutionScope::new(),
            OpenDirectory::new(Arc::clone(self) as Arc<dyn ImmutableConnectionClient>),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            server_end.into_channel().into(),
        );

        proxy
    }

    async fn packages(&self) -> HashMap<PackageName, HashMap<PackageVariant, Hash>> {
        let mut res: HashMap<PackageName, HashMap<PackageVariant, Hash>> = HashMap::new();

        // First populate with static packages.
        for (path, hash) in self.static_packages.contents() {
            let name = path.name().to_owned();
            let variant = path.variant().to_owned();

            res.entry(name).or_default().insert(variant, *hash);
        }

        // Then fill in allowed dynamic packages, which may override existing static packages.
        let active_packages = self.non_static_packages.lock().await.active_packages();
        for (path, hash) in active_packages {
            if !self.non_static_allow_list.allows(path.name()) {
                continue;
            }

            let name = path.name().to_owned();
            let variant = path.variant().to_owned();

            res.entry(name).or_default().insert(variant, hash);
        }

        res
    }

    async fn package_variants(&self, name: &PackageName) -> Option<HashMap<PackageVariant, Hash>> {
        self.packages().await.remove(name)
    }

    async fn package_names(&self) -> BTreeSet<PackageName> {
        self.packages().await.into_iter().map(|(k, _)| k).collect()
    }
}

impl DirectoryEntry for PkgfsPackages {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        mut path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        // This directory and all child nodes are read-only
        if flags
            & (OPEN_RIGHT_WRITABLE
                | OPEN_RIGHT_ADMIN
                | OPEN_FLAG_CREATE
                | OPEN_FLAG_CREATE_IF_ABSENT
                | OPEN_FLAG_TRUNCATE
                | OPEN_FLAG_APPEND)
            != 0
        {
            return send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
        }

        scope.clone().spawn(async move {
            match path.next().map(PackageName::from_str) {
                None => ImmutableConnection::create_connection(
                    scope,
                    OpenDirectory::new(self as Arc<dyn ImmutableConnectionClient>),
                    flags,
                    server_end,
                ),
                Some(Ok(package_name)) => match self.package_variants(&package_name).await {
                    Some(variants) => Arc::new(PkgfsPackagesVariants {
                        contents: variants,
                        blobfs: self.blobfs.clone(),
                    })
                    .open(scope, flags, mode, path, server_end),
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
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
}

#[async_trait]
impl Directory for PkgfsPackages {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<(dyn dirents_sink::Sink + 'static)>,
    ) -> Result<(TraversalPosition, Box<(dyn dirents_sink::Sealed + 'static)>), zx::Status> {
        use dirents_sink::AppendResult;

        let entries = self.package_names().await;

        let mut remaining = match pos {
            TraversalPosition::Start => {
                // Yield "." first. If even that can't fit in the response, return the same
                // traversal position so we try again next time (where the client hopefully
                // provides a bigger buffer).
                match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), ".") {
                    AppendResult::Ok(new_sink) => sink = new_sink,
                    AppendResult::Sealed(sealed) => return Ok((TraversalPosition::Start, sealed)),
                }

                entries.range::<PackageName, _>(..)
            }
            TraversalPosition::Name(next) => {
                // This function only returns valid package names, so it will always be provided a
                // valid package name.
                let next: PackageName = next.parse().unwrap();

                // `next` is the name of the next item that still needs to be provided, so start
                // there.
                entries.range(next..)
            }
            TraversalPosition::Index(_) => {
                // This directory uses names for iteration to more gracefully handle concurrent
                // directory reads while the directory itself may change.  Index-based enumeration
                // may end up repeating elements (or panic if this allowed deleting directory
                // entries).  Name-based enumeration may give a temporally inconsistent view of the
                // directory, so neither approach is ideal.
                unreachable!()
            }
            TraversalPosition::End => return Ok((TraversalPosition::End, sink.seal().into())),
        };

        while let Some(name) = remaining.next() {
            let name = name.to_string();
            match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), &name) {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    // Ran out of response buffer space. Pick up on this item next time.
                    return Ok((TraversalPosition::Name(name), sealed));
                }
            }
        }

        Ok((TraversalPosition::End, sink.seal()))
    }

    fn register_watcher(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: fidl::AsyncChannel,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so this is a no-op.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<NodeAttributes, zx::Status> {
        Ok(NodeAttributes {
            mode: MODE_TYPE_DIRECTORY
                | rights_to_posix_mode_bits(/*r*/ true, /*w*/ false, /*x*/ true),
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
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        fuchsia_pkg::PackagePath,
        maplit::{convert_args, hashmap},
        matches::assert_matches,
    };

    impl PkgfsPackages {
        pub fn new_test(
            static_packages: system_image::StaticPackages,
            non_static_allow_list: NonStaticAllowList,
        ) -> (Arc<Self>, Arc<Mutex<PackageIndex>>) {
            let (blobfs, _) = blobfs::Client::new_mock();
            let index = Arc::new(Mutex::new(PackageIndex::new_test()));

            (
                Arc::new(PkgfsPackages::new(
                    static_packages,
                    Arc::clone(&index),
                    non_static_allow_list,
                    blobfs,
                )),
                index,
            )
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
        let (pkgfs_packages, _package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![]),
            non_static_allow_list(&[]),
        );

        drop(pkgfs_packages);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn packages_listing_unions_indices() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![(path("static", "0"), hash(0))]),
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
    async fn packages_listing_dynamic_overrides_static() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![
                (path("replaceme", "0"), hash(0)),
                (path("replaceme", "butnotme"), hash(1)),
            ]),
            non_static_allow_list(&["replaceme"]),
        );

        register_dynamic_package(&package_index, path("replaceme", "0"), hash(10)).await;

        assert_eq!(
            pkgfs_packages.packages().await,
            package_name_hashmap!(
                "replaceme" => package_variant_hashmap!{
                    "0" => hash(10),
                    "butnotme" => hash(1),
                },
            )
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn packages_listing_ignores_disallowed_dynamic_packages() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![
                (path("allowed", "0"), hash(0)),
                (path("static", "0"), hash(1)),
            ]),
            non_static_allow_list(&["allowed"]),
        );

        register_dynamic_package(&package_index, path("allowed", "0"), hash(10)).await;
        register_dynamic_package(&package_index, path("static", "0"), hash(11)).await;
        register_dynamic_package(&package_index, path("dynamic", "0"), hash(12)).await;

        assert_eq!(
            pkgfs_packages.packages().await,
            package_name_hashmap!(
                "allowed" => package_variant_hashmap!{
                    "0" => hash(10),
                },
                "static" => package_variant_hashmap!{
                    "0" => hash(1),
                },
            )
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_empty() {
        let (pkgfs_packages, _package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![]),
            non_static_allow_list(&[]),
        );

        // An empty readdir buffer can't hold any elements, so it returns nothing and indicates we
        // are still at the start.
        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_packages,
            &TraversalPosition::Start,
            Box::new(FakeSink::new(0)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);
        assert_eq!(pos, TraversalPosition::Start);

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
            vec![(".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_enumerates_all_allowed_entries() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![
                (path("allowed", "0"), hash(0)),
                (path("static", "0"), hash(1)),
                (path("static", "1"), hash(2)),
                (path("still", "static"), hash(3)),
            ]),
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
                (".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("allowed".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("dynonly".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("static".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("still".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_one_entry_at_a_time_yields_expected_entries() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![
                (path("allowed", "0"), hash(0)),
                (path("static", "0"), hash(1)),
                (path("static", "1"), hash(2)),
                (path("still", "static"), hash(3)),
            ]),
            non_static_allow_list(&["allowed", "missing"]),
        );

        register_dynamic_package(&package_index, path("allowed", "dynamic-package"), hash(10))
            .await;
        register_dynamic_package(&package_index, path("static", "0"), hash(11)).await;
        register_dynamic_package(&package_index, path("dynamic", "0"), hash(12)).await;

        let expected_entries = vec![
            (
                ".".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("allowed".to_owned()),
            ),
            (
                "allowed".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("static".to_owned()),
            ),
            (
                "static".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("still".to_owned()),
            ),
            (
                "still".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::End,
            ),
        ];

        let mut pos = TraversalPosition::Start;

        for (name, info, expected_pos) in expected_entries {
            let (newpos, sealed) =
                Directory::read_dirents(&*pkgfs_packages, &pos, Box::new(FakeSink::new(1)))
                    .await
                    .expect("read_dirents failed");

            assert_eq!(FakeSink::from_sealed(sealed).entries, vec![(name, info)]);
            assert_eq!(newpos, expected_pos);

            pos = newpos;
        }
    }

    struct DirReader {
        dir: Arc<dyn Directory>,
        entries: Vec<(String, EntryInfo)>,
        pos: TraversalPosition,
    }

    impl DirReader {
        fn new(dir: Arc<dyn Directory>) -> Self {
            Self { dir, entries: vec![], pos: TraversalPosition::Start }
        }

        async fn read(&mut self, n: usize) {
            let (newpos, sealed) =
                Directory::read_dirents(&*self.dir, &self.pos, Box::new(FakeSink::new(n)))
                    .await
                    .unwrap();

            self.entries.extend(FakeSink::from_sealed(sealed).entries);
            self.pos = newpos;
        }

        fn unwrap_done(self) -> Vec<(String, EntryInfo)> {
            assert_eq!(self.pos, TraversalPosition::End);
            self.entries
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_pagination_may_encounter_temporal_anomalies() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![
                (path("b", "0"), hash(0)),
                (path("d", "0"), hash(1)),
            ]),
            non_static_allow_list(&["a", "c", "e"]),
        );

        // Register packages "a", "c", and "e" in sequence, but carefully interleave a readdir to
        // observe "e" but not "c".

        let mut reader = DirReader::new(pkgfs_packages);

        register_dynamic_package(&package_index, path("a", "0"), hash(2)).await;

        // Read ".", "a", "b".
        reader.read(3).await;

        register_dynamic_package(&package_index, path("c", "0"), hash(3)).await;
        register_dynamic_package(&package_index, path("e", "0"), hash(4)).await;

        // Read "d", "e".
        reader.read(2).await;

        assert_eq!(
            reader.unwrap_done(),
            vec![
                (".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("a".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("b".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("d".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("e".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_rejects_invalid_name() {
        let (pkgfs_packages, _package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![]),
            non_static_allow_list(&[]),
        );

        let proxy = pkgfs_packages.proxy();

        assert_matches!(
            io_util::directory::open_directory(
                &proxy,
                "invalidname-!@#$%^&*()+=",
                OPEN_RIGHT_READABLE
            )
            .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_rejects_missing_package() {
        let (pkgfs_packages, _package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![]),
            non_static_allow_list(&[]),
        );

        let proxy = pkgfs_packages.proxy();

        assert_matches!(
            io_util::directory::open_directory(&proxy, "missing", OPEN_RIGHT_READABLE).await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_opens_static_package_variants() {
        let (pkgfs_packages, _package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![(path("static", "0"), hash(0))]),
            non_static_allow_list(&[]),
        );

        let proxy = pkgfs_packages.proxy();

        assert_matches!(
            io_util::directory::open_directory(&proxy, "static", OPEN_RIGHT_READABLE).await,
            Err(io_util::node::OpenError::OpenError(zx::Status::TIMED_OUT))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_opens_allowed_dynamic_variants() {
        let (pkgfs_packages, package_index) = PkgfsPackages::new_test(
            system_image::StaticPackages::from_entries(vec![]),
            non_static_allow_list(&["dynamic"]),
        );

        let proxy = pkgfs_packages.proxy();

        assert_matches!(
            io_util::directory::open_directory(&proxy, "dynamic", OPEN_RIGHT_READABLE).await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );

        register_dynamic_package(&package_index, path("dynamic", "0"), hash(0)).await;

        assert_matches!(
            io_util::directory::open_directory(&proxy, "dynamic", OPEN_RIGHT_READABLE).await,
            Err(io_util::node::OpenError::OpenError(zx::Status::TIMED_OUT))
        );
    }
}
