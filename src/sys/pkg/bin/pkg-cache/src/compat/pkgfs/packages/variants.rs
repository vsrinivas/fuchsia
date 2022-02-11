// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::super::BitFlags as _,
    anyhow::anyhow,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MODE_TYPE_DIRECTORY,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_POSIX_DEPRECATED,
        OPEN_FLAG_POSIX_EXECUTABLE, OPEN_FLAG_POSIX_WRITABLE, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_hash::Hash,
    fuchsia_pkg::PackageVariant,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    std::{
        collections::{BTreeSet, HashMap},
        sync::Arc,
    },
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::io1::DerivedConnection,
            dirents_sink,
            entry::{DirectoryEntry, EntryInfo},
            entry_container::Directory,
            immutable::connection::io1::ImmutableConnection,
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path,
    },
};

#[derive(Debug)]
pub struct PkgfsPackagesVariants {
    contents: HashMap<PackageVariant, Hash>,
    blobfs: blobfs::Client,
}

impl PkgfsPackagesVariants {
    pub(super) fn new(contents: HashMap<PackageVariant, Hash>, blobfs: blobfs::Client) -> Self {
        Self { contents, blobfs }
    }

    fn variants(&self) -> BTreeSet<PackageVariant> {
        self.contents.keys().cloned().collect()
    }

    fn variant(&self, name: &str) -> Option<Hash> {
        self.contents.get(&name.parse().ok()?).cloned()
    }
}

impl DirectoryEntry for PkgfsPackagesVariants {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        mut path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let flags = flags.unset(OPEN_FLAG_POSIX_WRITABLE);
        let flags = if flags.is_any_set(OPEN_FLAG_POSIX_DEPRECATED) {
            flags.unset(OPEN_FLAG_POSIX_DEPRECATED).set(OPEN_FLAG_POSIX_EXECUTABLE)
        } else {
            flags
        };

        // This directory and all child nodes are read-only
        if flags.is_any_set(
            OPEN_RIGHT_WRITABLE
                | OPEN_FLAG_CREATE
                | OPEN_FLAG_CREATE_IF_ABSENT
                | OPEN_FLAG_TRUNCATE
                | OPEN_FLAG_APPEND,
        ) {
            return send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
        }

        match path.next().map(|variant| self.variant(variant)) {
            None => ImmutableConnection::create_connection(scope, self, flags, server_end),
            Some(Some(hash)) => {
                let blobfs = self.blobfs.clone();
                scope.clone().spawn(async move {
                    if let Err(e) = package_directory::serve_path(
                        scope, blobfs, hash, flags, mode, path, server_end,
                    )
                    .await
                    {
                        fx_log_err!(
                            "Failed to open package directory for {}: {:#}",
                            hash,
                            anyhow!(e)
                        );
                    }
                })
            }
            Some(None) => send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND),
        }
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
}

#[async_trait]
impl Directory for PkgfsPackagesVariants {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<(dyn dirents_sink::Sink + 'static)>,
    ) -> Result<(TraversalPosition, Box<(dyn dirents_sink::Sealed + 'static)>), zx::Status> {
        use dirents_sink::AppendResult;

        let entries = self.variants();

        let mut remaining = match pos {
            TraversalPosition::Start => {
                // Yield "." first. If even that can't fit in the response, return the same
                // traversal position so we try again next time (where the client hopefully
                // provides a bigger buffer).
                match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), ".") {
                    AppendResult::Ok(new_sink) => sink = new_sink,
                    AppendResult::Sealed(sealed) => return Ok((TraversalPosition::Start, sealed)),
                }

                entries.range::<PackageVariant, _>(..)
            }
            TraversalPosition::Name(next) => {
                // This function only returns valid package variants, so it will always be provided
                // a valid package variant.
                let next: PackageVariant = next.parse().unwrap();

                // `next` is the name of the next item that still needs to be provided, so start
                // there.
                entries.range(next..)
            }
            TraversalPosition::Index(_) => {
                // This directory uses names for iteration because I copy/pasted this from super.
                // Since this directory is immutable, index-based enumeration would be more
                // efficient.
                unreachable!()
            }
            TraversalPosition::End => return Ok((TraversalPosition::End, sink.seal().into())),
        };

        while let Some(variant) = remaining.next() {
            let variant = variant.to_string();
            match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), &variant) {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    // Ran out of response buffer space. Pick up on this item next time.
                    return Ok((TraversalPosition::Name(variant), sealed));
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
            mode: MODE_TYPE_DIRECTORY,
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
        crate::compat::pkgfs::testing::FakeSink,
        assert_matches::assert_matches,
        fidl_fuchsia_io::{OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE},
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        maplit::{btreeset, convert_args, hashmap},
        std::str::FromStr,
    };

    impl PkgfsPackagesVariants {
        pub fn new_test(contents: HashMap<PackageVariant, Hash>) -> Arc<Self> {
            let (blobfs, _) = blobfs::Client::new_mock();

            Arc::new(PkgfsPackagesVariants::new(contents, blobfs))
        }

        fn proxy(self: &Arc<Self>, flags: u32) -> fidl_fuchsia_io::DirectoryProxy {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();

            vfs::directory::entry::DirectoryEntry::open(
                Arc::clone(&self),
                ExecutionScope::new(),
                flags,
                0,
                Path::dot(),
                server_end.into_channel().into(),
            );

            proxy
        }
    }

    macro_rules! package_variant_hashmap {
        ($($inner:tt)*) => {
            convert_args!(
                keys = |s| PackageVariant::from_str(s).unwrap(),
                hashmap!($($inner)*)
            )
        };
    }

    fn hash(n: u8) -> Hash {
        Hash::from([n; 32])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn minimal_lifecycle() {
        let pkgfs_packages_variants = PkgfsPackagesVariants::new_test(hashmap! {});

        drop(pkgfs_packages_variants);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn variants_listing_is_contents_keys() {
        let pkgfs_packages_variants = PkgfsPackagesVariants::new_test(package_variant_hashmap! {
            "0" => hash(0x0_0),
            "zero" => hash(0),
            "nil" => hash(0),
            "not-0" => hash(42),
        });

        assert_eq!(
            pkgfs_packages_variants.variants(),
            convert_args!(
                keys = |s| PackageVariant::from_str(s).unwrap(),
                btreeset!("0", "zero", "nil", "not-0")
            )
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn variant_getter_is_contents_lookup() {
        let pkgfs_packages_variants = PkgfsPackagesVariants::new_test(package_variant_hashmap! {
            "0" => hash(0),
            "1" => hash(1),
            "answer" => hash(42),
        });

        assert_eq!(pkgfs_packages_variants.variant("0"), Some(hash(0)));
        assert_eq!(pkgfs_packages_variants.variant("1"), Some(hash(1)));
        assert_eq!(pkgfs_packages_variants.variant("answer"), Some(hash(42)));

        assert_eq!(pkgfs_packages_variants.variant("unknown"), None);
        assert_eq!(pkgfs_packages_variants.variant("invalid-chars:!@#$%^&*()"), None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_empty() {
        let pkgfs_packages_variants = PkgfsPackagesVariants::new_test(package_variant_hashmap! {});

        // An empty readdir buffer can't hold any elements, so it returns nothing and indicates we
        // are still at the start.
        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_packages_variants,
            &TraversalPosition::Start,
            Box::new(FakeSink::new(0)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);
        assert_eq!(pos, TraversalPosition::Start);

        // Given adequate buffer space, the only entry is itself (".").
        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_packages_variants,
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
    async fn readdir_enumerates_all_variants() {
        let pkgfs_packages_variants = PkgfsPackagesVariants::new_test(package_variant_hashmap! {
            "0" => hash(0),
            "1" => hash(1),
            "two" => hash(2),
        });

        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_packages_variants,
            &TraversalPosition::Start,
            Box::new(FakeSink::new(100)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![
                (".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("0".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("1".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("two".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_one_entry_at_a_time_yields_expected_entries() {
        let pkgfs_packages_variants = PkgfsPackagesVariants::new_test(package_variant_hashmap! {
            "0" => hash(0),
            "1" => hash(1),
            "two" => hash(2),
        });

        let expected_entries = vec![
            (
                ".".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("0".to_owned()),
            ),
            (
                "0".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("1".to_owned()),
            ),
            (
                "1".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("two".to_owned()),
            ),
            (
                "two".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::End,
            ),
        ];

        let mut pos = TraversalPosition::Start;

        for (name, info, expected_pos) in expected_entries {
            let (newpos, sealed) = Directory::read_dirents(
                &*pkgfs_packages_variants,
                &pos,
                Box::new(FakeSink::new(1)),
            )
            .await
            .expect("read_dirents failed");

            assert_eq!(FakeSink::from_sealed(sealed).entries, vec![(name, info)]);
            assert_eq!(newpos, expected_pos);

            pos = newpos;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_rejects_invalid_name() {
        let pkgfs_packages_variants = PkgfsPackagesVariants::new_test(package_variant_hashmap! {});

        let proxy = pkgfs_packages_variants.proxy(OPEN_RIGHT_READABLE);

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
        let pkgfs_packages_variants = PkgfsPackagesVariants::new_test(package_variant_hashmap! {});

        let proxy = pkgfs_packages_variants.proxy(OPEN_RIGHT_READABLE);

        assert_matches!(
            io_util::directory::open_directory(&proxy, "missing", OPEN_RIGHT_READABLE).await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_unsets_posix_writable() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let pkgfs_packages_variants = Arc::new(PkgfsPackagesVariants::new(
            package_variant_hashmap! {
                "0" => metafar_blob.merkle,
            },
            blobfs_client,
        ));

        let proxy = pkgfs_packages_variants.proxy(OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX_WRITABLE);

        let (status, flags) = proxy.get_flags().await.unwrap();
        let () = zx::Status::ok(status).unwrap();
        assert_eq!(flags, OPEN_RIGHT_READABLE);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_converts_posix_deprecated_to_posix_exec() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let pkgfs_packages_variants = Arc::new(PkgfsPackagesVariants::new(
            package_variant_hashmap! {
                "0" => metafar_blob.merkle,
            },
            blobfs_client,
        ));

        let proxy = pkgfs_packages_variants.proxy(OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX_DEPRECATED);

        let (status, flags) = proxy.get_flags().await.unwrap();
        let () = zx::Status::ok(status).unwrap();
        assert_eq!(flags, OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_opens_known_package_variant() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let pkgfs_packages_variants = Arc::new(PkgfsPackagesVariants::new(
            package_variant_hashmap! {
                "0" => metafar_blob.merkle,
            },
            blobfs_client,
        ));

        let proxy = pkgfs_packages_variants.proxy(OPEN_RIGHT_READABLE);

        let dir =
            io_util::directory::open_directory(&proxy, "0", OPEN_RIGHT_READABLE).await.unwrap();
        let () = package.verify_contents(&dir).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_opens_path_within_known_package_variant() {
        let package = PackageBuilder::new("just-meta-far")
            .add_resource_at("meta/message", &b"Hello World!"[..])
            .build()
            .await
            .expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let pkgfs_packages_variants = Arc::new(PkgfsPackagesVariants::new(
            package_variant_hashmap! {
                "0" => metafar_blob.merkle,
            },
            blobfs_client,
        ));

        let proxy = pkgfs_packages_variants.proxy(OPEN_RIGHT_READABLE);

        let file = io_util::directory::open_file(&proxy, "0/meta/message", OPEN_RIGHT_READABLE)
            .await
            .unwrap();
        let message = io_util::file::read_to_string(&file).await.unwrap();
        assert_eq!(message, "Hello World!");
    }
}
