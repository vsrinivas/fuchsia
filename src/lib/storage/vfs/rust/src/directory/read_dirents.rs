// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A sink that can consume directory entry information, encoding them as expected by `fuchsia.io`
//! `Directory::ReadDirents` result.

use crate::directory::{
    common::encode_dirent,
    dirents_sink::{self, AppendResult},
    entry::EntryInfo,
    traversal_position::TraversalPosition,
};

use {fidl_fuchsia_io as fio, fuchsia_zircon::Status, std::any::Any, std::convert::TryInto as _};

/// An instance of this type represents a sink that may still accept additional entries.  Depending
/// on the entry size it may turn itself into a [`Done`] value, indicating that the internal buffer
/// is full.
pub struct Sink {
    buf: Vec<u8>,
    max_bytes: u64,
    state: SinkState,
}

/// An instance of this type is a `ReadDirents` result sink that is full and may not consume any
/// more values.
pub struct Done {
    pub(super) buf: Vec<u8>,
    pub(super) status: Status,
}

#[derive(PartialEq, Eq)]
enum SinkState {
    NotCalled,
    DidNotFit,
    FitOne,
}

impl Sink {
    /// Constructs a new sync that will have the specified number of bytes of storage.
    pub(super) fn new(max_bytes: u64) -> Box<Sink> {
        Box::new(Sink { buf: vec![], max_bytes, state: SinkState::NotCalled })
    }
}

impl dirents_sink::Sink for Sink {
    fn append(mut self: Box<Self>, entry: &EntryInfo, name: &str) -> AppendResult {
        if !encode_dirent(&mut self.buf, self.max_bytes, entry, name) {
            if self.state == SinkState::NotCalled {
                self.state = SinkState::DidNotFit;
            }
            AppendResult::Sealed(self.seal())
        } else {
            if self.state == SinkState::NotCalled {
                self.state = SinkState::FitOne;
            }
            AppendResult::Ok(self)
        }
    }

    fn seal(self: Box<Self>) -> Box<dyn dirents_sink::Sealed> {
        Box::new(Done {
            buf: self.buf,
            status: match self.state {
                SinkState::NotCalled | SinkState::FitOne => Status::OK,
                SinkState::DidNotFit => Status::BUFFER_TOO_SMALL,
            },
        })
    }
}

impl dirents_sink::Sealed for Done {
    fn open(self: Box<Self>) -> Box<dyn Any> {
        self
    }
}

/// Helper for implementing `crate::directory::entry_container::Directory::read_dirents`.
///
/// `entries` must be the contents of the directory sorted by name, the second tuple element.
/// `entries` must not contain ".". This fn will append "." to `sink` as the first element
///   automatically using inode `fidl_fuchsia_io::INO_UNKNOWN`.
pub async fn read_dirents<'a>(
    entries: &'a [(EntryInfo, String)],
    pos: &'a TraversalPosition,
    mut sink: Box<(dyn dirents_sink::Sink + 'static)>,
) -> Result<(TraversalPosition, Box<(dyn dirents_sink::Sealed + 'static)>), Status> {
    let starting_position = match pos {
        TraversalPosition::Start => {
            match sink.append(&EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), ".") {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    return Ok((TraversalPosition::Start, sealed));
                }
            };
            0usize
        }
        TraversalPosition::Index(i) => u64_to_usize_safe(*i),
        TraversalPosition::End => {
            return Ok((TraversalPosition::End, sink.seal()));
        }
        TraversalPosition::Name(_) => {
            unreachable!("the VFS should never send this to us, since we never return it here");
        }
    };

    for i in starting_position..entries.len() {
        let (info, name) = &entries[i];
        match sink.append(info, name) {
            AppendResult::Ok(new_sink) => sink = new_sink,
            AppendResult::Sealed(sealed) => {
                return Ok((TraversalPosition::Index(usize_to_u64_safe(i)), sealed));
            }
        }
    }
    Ok((TraversalPosition::End, sink.seal()))
}

fn usize_to_u64_safe(u: usize) -> u64 {
    let ret: u64 = u.try_into().unwrap();
    static_assertions::assert_eq_size_val!(u, ret);
    ret
}

fn u64_to_usize_safe(u: u64) -> usize {
    let ret: usize = u.try_into().unwrap();
    static_assertions::assert_eq_size_val!(u, ret);
    ret
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn usize_to_u64_safe_does_not_panic() {
        assert_eq!(usize_to_u64_safe(usize::MAX), usize::MAX as u64);
    }

    #[test]
    fn u64_to_usize_safe_does_not_panic() {
        assert_eq!(u64_to_usize_safe(u64::MAX), u64::MAX as usize);
    }

    /// Implementation of `crate::directory::dirents_sink::Sink`.
    /// `Sink::append` begins to fail (return Sealed) after `max_entries` entries have been
    /// appended.
    #[derive(Clone)]
    pub(crate) struct FakeSink {
        max_entries: usize,
        entries: Vec<(EntryInfo, String)>,
        sealed: bool,
    }

    impl FakeSink {
        fn new(max_entries: usize) -> Self {
            FakeSink { max_entries, entries: Vec::with_capacity(max_entries), sealed: false }
        }

        fn from_sealed(sealed: Box<dyn dirents_sink::Sealed>) -> Box<FakeSink> {
            sealed.into()
        }
    }

    impl From<Box<dyn dirents_sink::Sealed>> for Box<FakeSink> {
        fn from(sealed: Box<dyn dirents_sink::Sealed>) -> Self {
            sealed.open().downcast::<FakeSink>().unwrap()
        }
    }

    impl dirents_sink::Sink for FakeSink {
        fn append(mut self: Box<Self>, entry: &EntryInfo, name: &str) -> AppendResult {
            assert!(!self.sealed);
            if self.entries.len() == self.max_entries {
                AppendResult::Sealed(self.seal())
            } else {
                self.entries.push((entry.clone(), name.to_owned()));
                AppendResult::Ok(self)
            }
        }

        fn seal(mut self: Box<Self>) -> Box<dyn dirents_sink::Sealed> {
            self.sealed = true;
            self
        }
    }

    impl dirents_sink::Sealed for FakeSink {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            self
        }
    }

    #[fuchsia::test]
    async fn read_dirents_start() {
        let entries = vec![
            (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), "dir".into()),
            (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File), "file".into()),
        ];

        // No space in sink.
        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(0)))
                .await
                .expect("read_dirents failed");
        assert_eq!(pos, TraversalPosition::Start);
        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);

        // Only enough space in sink for partial write.
        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(2)))
                .await
                .expect("read_dirents failed");
        assert_eq!(pos, TraversalPosition::Index(1));
        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![
                (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), ".".into()),
                (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), "dir".into()),
            ]
        );

        // Enough space in sink for complete write.
        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(3)))
                .await
                .expect("read_dirents failed");
        assert_eq!(pos, TraversalPosition::End);
        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![
                (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), ".".into()),
                (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), "dir".into()),
                (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File), "file".into())
            ]
        );
    }

    #[fuchsia::test]
    async fn read_dirents_index() {
        let entries = vec![
            (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), "dir".into()),
            (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File), "file".into()),
        ];

        // No space in sink.
        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Index(0), Box::new(FakeSink::new(0)))
                .await
                .expect("read_dirents failed");
        assert_eq!(pos, TraversalPosition::Index(0));
        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);

        // Only enough space in sink for partial write.
        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Index(0), Box::new(FakeSink::new(1)))
                .await
                .expect("read_dirents failed");
        assert_eq!(pos, TraversalPosition::Index(1));
        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![(EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), "dir".into()),]
        );

        // Enough space in sink for complete write.
        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Index(0), Box::new(FakeSink::new(2)))
                .await
                .expect("read_dirents failed");
        assert_eq!(pos, TraversalPosition::End);
        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![
                (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), "dir".into()),
                (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File), "file".into()),
            ]
        );
    }

    #[fuchsia::test]
    async fn read_dirents_end() {
        let entries = vec![
            (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), "dir".into()),
            (EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File), "file".into()),
        ];

        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::End, Box::new(FakeSink::new(3)))
                .await
                .expect("read_dirents failed");
        assert_eq!(pos, TraversalPosition::End);
        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);
    }
}
