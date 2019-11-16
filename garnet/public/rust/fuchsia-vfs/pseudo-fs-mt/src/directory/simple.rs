// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is an implementation of "simple" pseudo directories.  Use [`directory::immutable::simple`]
//! to construct actual instances.  See [`Simple`] for details.

use crate::{
    common::send_on_open_with_error,
    directory::{
        connection::{create_connection, DerivedConnection},
        dirents_sink,
        entry::{DirectoryEntry, EntryInfo},
        entry_container::{self, AsyncReadDirents},
        immutable::connection::{ImmutableConnection, ImmutableConnectionClient},
        traversal_position::AlphabeticalTraversal,
        watchers::{
            event_producers::{SingleNameEventProducer, StaticVecEventProducer},
            Watchers,
        },
    },
    execution_scope::ExecutionScope,
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MAX_FILENAME},
    fuchsia_async::Channel,
    fuchsia_zircon::Status,
    parking_lot::Mutex,
    static_assertions::assert_eq_size,
    std::{collections::BTreeMap, iter, marker::PhantomData, sync::Arc},
};

/// An implementation of a "simple" pseudo directory.  This directory holds a set of entries,
/// allowing the server to add or remove entries via the [`add_entry`] and
/// [`remove_entry`] methods, and, depending on the connection been used (see
/// [`directory::immutable::connection::ImmutableConnection`] or
/// [`directory::mutable::connection::MutableConnection`])
/// it may also allow the clients to modify the entries as well.  This is a common implemmentation
/// for [`directory::immutable::simple`] and [`directory::mutable::simple`].
pub struct Simple<Connection>
where
    Connection: DerivedConnection<AlphabeticalTraversal> + 'static,
{
    inner: Mutex<Inner>,

    // I wish this field would not be here.  A directory instance already knows the type of the
    // connections that connect to it - it is in the `Connection` type.  So we should just be able
    // to use that.  But, unfortunately, I could not write `DirectoryEntry::open()` implementation.
    // The compiler is confused when it needs to convert `self` into an `Arc<dyn
    // ImmutableConnectionClient>` or `Arc<dyn MutableConnectionClient>`.  I have tried a few
    // tricks to add the necessary constraints, but I was not able to write it correctly.
    // Essentially the constraint should be something like this:
    //
    //     impl<Connection> DirectoryEntry for Simple<Connection>
    //     where
    //         Connection: DerivedConnection<AlphabeticalTraversal> + 'static,
    //         Arc<Self>: IsConvertableTo<
    //             Type = Arc<
    //                 <Connection as DerivedConnection<AlphabeticalTraversal>>::Directory
    //             >
    //         >
    //
    // The problem is that I do not know how to write this `IsConvertableTo` trait.  Compiler seems
    // to be following some special rules when you say `A as Arc<B>` (when `A` is an `Arc`), as it
    // allows subtyping, but I do not know how to express the same constraint.
    mutable: bool,

    _connection: PhantomData<Connection>,
}

struct Inner {
    entries: BTreeMap<String, Arc<dyn DirectoryEntry>>,

    watchers: Watchers,
}

impl<Connection> Simple<Connection>
where
    Connection: DerivedConnection<AlphabeticalTraversal> + 'static,
{
    pub(super) fn new(mutable: bool) -> Arc<Self> {
        Arc::new(Simple {
            inner: Mutex::new(Inner { entries: BTreeMap::new(), watchers: Watchers::new() }),
            mutable,
            _connection: PhantomData,
        })
    }
}

impl<Connection> DirectoryEntry for Simple<Connection>
where
    Connection: DerivedConnection<AlphabeticalTraversal> + 'static,
{
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
                    // See comment above `Simple::mutable` as to why this selection is necessary.
                    if self.mutable {
                        panic!("Not implemented");
                    } else {
                        create_connection::<
                            ImmutableConnection<AlphabeticalTraversal>,
                            AlphabeticalTraversal,
                        >(
                            scope,
                            self as Arc<dyn ImmutableConnectionClient<AlphabeticalTraversal>>,
                            flags,
                            mode,
                            server_end,
                        );
                    }
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

    fn can_hardlink(&self) -> bool {
        false
    }
}

impl<Connection> entry_container::DirectlyMutable for Simple<Connection>
where
    Connection: DerivedConnection<AlphabeticalTraversal> + 'static,
{
    fn add_entry_impl(
        self: Arc<Self>,
        name: String,
        entry: Arc<dyn DirectoryEntry>,
    ) -> Result<(), Status> {
        assert_eq_size!(u64, usize);
        if name.len() as u64 > MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        let mut this = self.inner.lock();

        if this.entries.contains_key(&name) {
            return Err(Status::ALREADY_EXISTS);
        }

        this.watchers.send_event(&mut SingleNameEventProducer::added(&name));

        let _ = this.entries.insert(name, entry);
        Ok(())
    }

    fn remove_entry_impl(
        self: Arc<Self>,
        name: String,
    ) -> Result<Option<Arc<dyn DirectoryEntry>>, Status> {
        assert_eq_size!(u64, usize);
        if name.len() as u64 >= MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        let mut this = self.inner.lock();

        this.watchers.send_event(&mut SingleNameEventProducer::removed(&name));

        Ok(this.entries.remove(&name))
    }
}

impl<Connection> entry_container::Observable<AlphabeticalTraversal> for Simple<Connection>
where
    Connection: DerivedConnection<AlphabeticalTraversal> + 'static,
{
    fn read_dirents(
        self: Arc<Self>,
        pos: AlphabeticalTraversal,
        sink: Box<dyn dirents_sink::Sink<AlphabeticalTraversal>>,
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

        let mut names = StaticVecEventProducer::existing({
            let entry_names = this.entries.keys();
            iter::once(&".".to_string()).chain(entry_names).cloned().collect()
        });

        if let Some(controller) = this.watchers.add(scope, self.clone(), mask, channel) {
            controller.send_event(&mut names);
            controller.send_event(&mut SingleNameEventProducer::idle());
        }

        Status::OK
    }

    fn unregister_watcher(self: Arc<Self>, key: usize) {
        let mut this = self.inner.lock();
        this.watchers.remove(key);
    }
}
