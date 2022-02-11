// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    fidl_fuchsia_io::{DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN},
    fuchsia_zircon as zx,
    std::{collections::BTreeMap, sync::Arc},
    vfs::directory::{
        dirents_sink,
        entry::{DirectoryEntry, EntryInfo},
        helper::DirectlyMutable as _,
        traversal_position::TraversalPosition,
    },
};

mod packages;
mod validation;
mod versions;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DirentType {
    Directory,
    File,
}

impl From<DirentType> for u8 {
    fn from(type_: DirentType) -> Self {
        match type_ {
            DirentType::Directory => DIRENT_TYPE_DIRECTORY,
            DirentType::File => DIRENT_TYPE_FILE,
        }
    }
}

// Helper for implementing vfs::directory::entry_container::Directory::read_dirents.
// `entries` keys are the names of the directory entries.
//
// If `entries` changes in between successive calls made while handling a paginated listing, i.e.
// if a directory's contents are changed while a client is handling a paginated
// fuchsia.io/Directory.ReadDirents call, i.e. if the input `pos` is from the result of a previous
// call and `entries` is not the same as on the previous call, clients are not guaranteed to see
// a consistent snapshot of the directory contents.
async fn read_dirents<'a>(
    entries: &'a BTreeMap<String, DirentType>,
    pos: &'a TraversalPosition,
    mut sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
) -> Result<
    (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
    zx::Status,
> {
    use dirents_sink::AppendResult;
    let mut remaining = match pos {
        TraversalPosition::Start => {
            // Yield "." first. If even that can't fit in the response, return the same
            // traversal position so we try again next time (where the client hopefully
            // provides a bigger buffer).
            match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), ".") {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => return Ok((TraversalPosition::Start, sealed)),
            }
            entries.range::<String, _>(..)
        }
        TraversalPosition::Name(next) => {
            // `next` is the name of the next item that still needs to be provided, so start
            // there.
            entries.range::<String, _>(next..)
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

    while let Some((next, dirent_type)) = remaining.next() {
        match sink.append(&EntryInfo::new(INO_UNKNOWN, (*dirent_type).into()), next) {
            AppendResult::Ok(new_sink) => sink = new_sink,
            AppendResult::Sealed(sealed) => {
                // Ran out of response buffer space. Pick up on this item next time.
                return Ok((TraversalPosition::Name(next.to_string()), sealed));
            }
        }
    }

    Ok((TraversalPosition::End, sink.seal()))
}

/// Make the pkgfs compatibility directory, which has the following structure:
///   ./
///     system/
///     packages/
///     versions/
///     ctl/
///       validation/
///         missing
/// The "system" directory will only be added and served if `system_image.is_some()`.
pub fn make_dir(
    base_packages: Arc<crate::BasePackages>,
    package_index: Arc<futures::lock::Mutex<crate::PackageIndex>>,
    non_static_allow_list: Arc<system_image::NonStaticAllowList>,
    executability_restrictions: system_image::ExecutabilityRestrictions,
    blobfs: blobfs::Client,
    system_image: Option<system_image::SystemImage>,
) -> Result<Arc<dyn DirectoryEntry>, anyhow::Error> {
    let dir = vfs::pseudo_directory! {
        "packages" => Arc::new(packages::PkgfsPackages::new(
            Arc::clone(&base_packages),
            Arc::clone(&package_index),
            Arc::clone(&non_static_allow_list),
            blobfs.clone(),
        )),
        "versions" => Arc::new(versions::PkgfsVersions::new(
            Arc::clone(&base_packages),
            Arc::clone(&package_index),
            Arc::clone(&non_static_allow_list),
            executability_restrictions,
            blobfs.clone(),
        )),
        "ctl" => vfs::pseudo_directory! {
            "validation" => Arc::new(validation::Validation::new(
                blobfs.clone(),
                base_packages.list_blobs().to_owned()
            ))
        },
    };

    if let Some(system_image) = system_image {
        let () = dir
            .add_entry("system", Arc::new(system_image.into_root_dir()))
            .context("adding system directory to pkgfs")?;
    }

    Ok(dir)
}

/// Extension trait for convenient manipulation of unsigned integers acting as flags, e.g.
/// fuchsia.io/Directory.Open flags.
trait BitFlags {
    /// Return `self` with `flags` set.
    #[must_use]
    fn set(self, flags: Self) -> Self;

    /// Return `self` with `flags` not set.
    #[must_use]
    fn unset(self, flags: Self) -> Self;

    /// Return `true` iff any of `flags` are set in `self`.
    #[must_use]
    fn is_any_set(self, flags: Self) -> bool;
}

impl BitFlags for u32 {
    fn set(self, flags: Self) -> Self {
        self | flags
    }

    fn unset(self, flags: Self) -> Self {
        self & !flags
    }

    fn is_any_set(self, flags: Self) -> bool {
        self & flags != 0
    }
}

#[cfg(test)]
mod testing {
    use {
        super::*,
        std::any::Any,
        vfs::directory::{
            dirents_sink::{AppendResult, Sealed, Sink},
            entry::EntryInfo,
        },
    };

    /// Implementation of vfs::directory::dirents_sink::Sink.
    /// Sink::append begins to fail (returns Sealed) after `max_entries` entries have been appended.
    #[derive(Clone)]
    pub(crate) struct FakeSink {
        max_entries: usize,
        pub(crate) entries: Vec<(String, EntryInfo)>,
        sealed: bool,
    }

    impl FakeSink {
        pub(crate) fn new(max_entries: usize) -> Self {
            FakeSink { max_entries, entries: Vec::with_capacity(max_entries), sealed: false }
        }

        pub(crate) fn from_sealed(sealed: Box<dyn Sealed>) -> Box<FakeSink> {
            sealed.into()
        }
    }

    impl From<Box<dyn Sealed>> for Box<FakeSink> {
        fn from(sealed: Box<dyn Sealed>) -> Self {
            sealed.open().downcast::<FakeSink>().unwrap()
        }
    }

    impl Sink for FakeSink {
        fn append(mut self: Box<Self>, entry: &EntryInfo, name: &str) -> AppendResult {
            assert!(!self.sealed);
            if self.entries.len() == self.max_entries {
                AppendResult::Sealed(self.seal())
            } else {
                self.entries.push((name.to_owned(), entry.clone()));
                AppendResult::Ok(self)
            }
        }

        fn seal(mut self: Box<Self>) -> Box<dyn Sealed> {
            self.sealed = true;
            self
        }
    }

    impl Sealed for FakeSink {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            self
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_empty() {
        let entries = BTreeMap::new();

        // An empty readdir buffer can't hold any elements, so it returns nothing and indicates we
        // are still at the start.
        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(0)))
                .await
                .expect("read_dirents failed");

        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);
        assert_eq!(pos, TraversalPosition::Start);

        // Given adequate buffer space, the only entry is itself (".").
        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(100)))
                .await
                .expect("read_dirents failed");

        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![(".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_one_entry_at_a_time_yields_expected_entries() {
        let entries = BTreeMap::from([
            ("dir0".to_string(), DirentType::Directory),
            ("dir1".to_string(), DirentType::Directory),
            ("file".to_string(), DirentType::File),
        ]);

        let expected_entries = vec![
            (
                ".".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("dir0".to_owned()),
            ),
            (
                "dir0".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("dir1".to_owned()),
            ),
            (
                "dir1".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name("file".to_owned()),
            ),
            (
                "file".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE),
                TraversalPosition::End,
            ),
        ];

        let mut pos = TraversalPosition::Start;

        for (name, info, expected_pos) in expected_entries {
            let (newpos, sealed) = read_dirents(&entries, &pos, Box::new(FakeSink::new(1)))
                .await
                .expect("read_dirents failed");

            assert_eq!(FakeSink::from_sealed(sealed).entries, vec![(name, info)]);
            assert_eq!(newpos, expected_pos);

            pos = newpos;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_pagination_may_encounter_temporal_anomalies() {
        // First ReadDirents when directory contains [., a, c].
        let mut entries = BTreeMap::from([
            ("a".to_string(), DirentType::File),
            ("c".to_string(), DirentType::File),
        ]);

        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(2)))
                .await
                .expect("read_dirents failed");

        let mut results = FakeSink::from_sealed(sealed).entries;

        // Add [b, d] to directory.
        entries.insert("b".to_string(), DirentType::File);
        entries.insert("d".to_string(), DirentType::File);

        // Finish ReadDirents.
        let (pos, sealed) = read_dirents(&entries, &pos, Box::new(FakeSink::new(10)))
            .await
            .expect("read_dirents failed");

        results.append(&mut FakeSink::from_sealed(sealed).entries);

        // b missing but d seen.
        assert_eq!(
            results,
            vec![
                (".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("a".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                ("c".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                ("d".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    const NONE: u32 = 0;
    const A: u32 = 0b001;
    const B: u32 = 0b010;
    const C: u32 = 0b100;
    const ALL: u32 = 0b111;

    #[test]
    fn bitflags_u32_set() {
        assert_eq!(NONE.set(A), A);
        assert_eq!(NONE.set(B), B);
        assert_eq!(NONE.set(C), C);

        assert_eq!(NONE.set(A | B), A | B);
        assert_eq!(NONE.set(A | C), A | C);
        assert_eq!(NONE.set(B | C), B | C);

        assert_eq!(NONE.set(A | B | C), A | B | C);

        assert_eq!(A.set(A), A);
        assert_eq!(A.set(B), A | B);
        assert_eq!(A.set(C), A | C);

        assert_eq!(A.set(A | B), A | B);
        assert_eq!(A.set(B | C), A | B | C);
        assert_eq!(A.set(A | C), A | C);

        assert_eq!(ALL.set(A), ALL);
        assert_eq!(ALL.set(A | B), ALL);
        assert_eq!(ALL.set(A | B | C), ALL);
    }

    #[test]
    fn bitflags_u32_unset() {
        assert_eq!(NONE.unset(A), NONE);
        assert_eq!(NONE.unset(B), NONE);
        assert_eq!(NONE.unset(C), NONE);
        assert_eq!(NONE.unset(A | B | C), NONE);

        assert_eq!(ALL.unset(A), B | C);
        assert_eq!(ALL.unset(B), A | C);
        assert_eq!(ALL.unset(C), A | B);

        assert_eq!(ALL.unset(A | B), C);
        assert_eq!(ALL.unset(A | C), B);
        assert_eq!(ALL.unset(B | C), A);

        assert_eq!(ALL.unset(A | B | C), NONE);

        assert_eq!(A.unset(A), NONE);
        assert_eq!(A.unset(B), A);
        assert_eq!(A.unset(C), A);

        assert_eq!(A.unset(A | B), NONE);
        assert_eq!(A.unset(B | C), A);
    }

    #[test]
    fn bitflags_u32_is_any_set() {
        assert_eq!(NONE.is_any_set(NONE), false);
        assert_eq!(NONE.is_any_set(A), false);
        assert_eq!(NONE.is_any_set(A | B), false);
        assert_eq!(NONE.is_any_set(A | B | C), false);

        assert_eq!(A.is_any_set(A), true);
        assert_eq!(A.is_any_set(B), false);
        assert_eq!(A.is_any_set(A | B), true);
        assert_eq!(A.is_any_set(B | C), false);
        assert_eq!(A.is_any_set(ALL), true);
        assert_eq!(A.is_any_set(NONE), false);

        assert_eq!(ALL.is_any_set(A), true);
        assert_eq!(ALL.is_any_set(A | B), true);
        assert_eq!(ALL.is_any_set(A | B | C), true);
        assert_eq!(ALL.is_any_set(NONE), false);
    }
}
