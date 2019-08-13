// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of a "simple" pseudo directory.  See [`Simple`] for details.

use crate::{
    common::send_on_open_with_error,
    directory::{
        connection::{AsyncReadDirents, DirectoryConnection, DirectoryEntryContainer},
        dirents_sink,
        entry::{DirectoryEntry, EntryInfo},
        traversal_position::AlphabeticalTraversal,
        watchers::Watchers,
    },
    execution_scope::ExecutionScope,
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MAX_FILENAME, WATCH_EVENT_ADDED,
        WATCH_EVENT_REMOVED, WATCH_MASK_ADDED, WATCH_MASK_REMOVED,
    },
    fuchsia_async::Channel,
    fuchsia_zircon::Status,
    parking_lot::Mutex,
    static_assertions::assert_eq_size,
    std::{collections::BTreeMap, iter, sync::Arc},
};

#[cfg(test)]
mod tests;

/// Creates an empty directory.
pub fn simple() -> Arc<Simple> {
    Simple::new()
}

/// An implementation of a "simple" pseudo directory.  This directory holds a "static" set of
/// entries, allowing the server to add or remove entries via the [`add_entry`] and
/// [`remove_entry`] methods.  Compare to the [`directory::lazy::Lazy`] directory, where the
/// entries are "dynamic" in a sense that a specific listing (and, potentially, the entries
/// themselves) are generated only when requested.
pub struct Simple {
    inner: Mutex<Inner>,
}

struct Inner {
    entries: BTreeMap<String, Arc<dyn DirectoryEntry>>,

    watchers: Watchers,
}

/// When in a "simple" directory is traversed, entries are returned in an alphanumeric order.
type SimpleDirectoryConnection = DirectoryConnection<AlphabeticalTraversal>;

impl Simple {
    fn new() -> Arc<Self> {
        Arc::new(Simple {
            inner: Mutex::new(Inner { entries: BTreeMap::new(), watchers: Watchers::new() }),
        })
    }

    /// Adds a child entry to this directory.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    pub fn add_entry<Name>(
        self: Arc<Self>,
        name: Name,
        entry: Arc<dyn DirectoryEntry>,
    ) -> Result<(), Status>
    where
        Name: Into<String>,
    {
        let name = name.into();

        assert_eq_size!(u64, usize);
        if name.len() as u64 >= MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        let mut this = self.inner.lock();

        if this.entries.contains_key(&name) {
            return Err(Status::ALREADY_EXISTS);
        }

        this.watchers.send_event(WATCH_MASK_ADDED, WATCH_EVENT_ADDED, name.clone());

        let _ = this.entries.insert(name, entry);
        Ok(())
    }

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    pub fn remove_entry<Name>(
        self: Arc<Self>,
        name: Name,
    ) -> Result<Option<Arc<dyn DirectoryEntry>>, Status>
    where
        Name: Into<String>,
    {
        let name = name.into();

        assert_eq_size!(u64, usize);
        if name.len() as u64 >= MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        let mut this = self.inner.lock();

        this.watchers.send_event(WATCH_MASK_REMOVED, WATCH_EVENT_REMOVED, name.clone());

        Ok(this.entries.remove(&name))
    }
}

impl DirectoryEntry for Simple {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        mut path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        // Do not hold the mutex more than necessary.  Plus, [`parking_lot::Mutex`] is not
        // re-entrant.  So we need to make sure to release the lock before we call `open()` is it
        // may turn out to be a recursive call, in case the directory contains itself directly or
        // through a number of other directories.
        let entry = {
            let name = match path.next() {
                Some(name) => name,
                None => {
                    SimpleDirectoryConnection::create_connection(
                        scope, self, flags, mode, server_end,
                    );
                    return;
                }
            };

            let this = self.inner.lock();
            let entry = match this.entries.get(name) {
                Some(entry) => entry,
                None => {
                    send_on_open_with_error(flags, server_end, Status::NOT_FOUND);
                    return;
                }
            };

            entry.clone()
        };

        entry.open(scope, flags, mode, path, server_end);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
}

impl DirectoryEntryContainer<AlphabeticalTraversal> for Simple {
    fn read_dirents(
        self: Arc<Self>,
        pos: AlphabeticalTraversal,
        sink: Box<dirents_sink::Sink<AlphabeticalTraversal>>,
    ) -> AsyncReadDirents {
        use dirents_sink::AppendResult;

        let this = self.inner.lock();

        let (mut sink, entries_iter) = match pos {
            AlphabeticalTraversal::Dot => {
                // Lazy position retrieval.
                let pos = &|| match this.entries.keys().next() {
                    None => AlphabeticalTraversal::End,
                    Some(first_name) => AlphabeticalTraversal::Name(first_name.clone()),
                };
                match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), ".", pos) {
                    AppendResult::Ok(sink) => {
                        // I wonder why, but rustc can not infer T in
                        //
                        //   pub fn range<T, R>(&self, range: R) -> Range<K, V>
                        //   where
                        //     K: Borrow<T>,
                        //     R: RangeBounds<T>,
                        //     T: Ord + ?Sized,
                        //
                        // for some reason here.  It says:
                        //
                        //   error[E0283]: type annotations required: cannot resolve `_: std::cmp::Ord`
                        //
                        // pointing to "range".  Same for two the other "range()" invocations
                        // below.
                        (sink, this.entries.range::<String, _>(..))
                    }
                    AppendResult::Sealed(sealed) => return sealed.into(),
                }
            }

            AlphabeticalTraversal::Name(next_name) => {
                (sink, this.entries.range::<String, _>(next_name..))
            }

            AlphabeticalTraversal::End => return sink.seal(AlphabeticalTraversal::End).into(),
        };

        for (name, entry) in entries_iter {
            match sink
                .append(&entry.entry_info(), &name, &|| AlphabeticalTraversal::Name(name.clone()))
            {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => return sealed.into(),
            }
        }

        sink.seal(AlphabeticalTraversal::End).into()
    }

    fn register_watcher(
        self: Arc<Self>,
        scope: ExecutionScope,
        mask: u32,
        channel: Channel,
    ) -> Status {
        let mut this = self.inner.lock();

        let names = {
            let entry_names = this.entries.keys();
            iter::once(&".".to_string()).chain(entry_names).cloned().collect()
        };
        this.watchers.add(scope, self.clone(), names, mask, channel);

        Status::OK
    }

    fn unregister_watcher(self: Arc<Self>, key: usize) {
        let mut this = self.inner.lock();
        this.watchers.remove(key);
    }
}
