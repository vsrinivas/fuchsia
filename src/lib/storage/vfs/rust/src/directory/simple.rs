// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is an implementation of "simple" pseudo directories.  Use [`directory::immutable::simple`]
//! to construct actual instances.  See [`Simple`] for details.

use crate::{
    common::send_on_open_with_error,
    directory::{
        connection::DerivedConnection,
        dirents_sink,
        entry::{DirectoryEntry, EntryInfo},
        entry_container::{self, AsyncGetEntry, AsyncReadDirents, EntryContainer},
        immutable::connection::{ImmutableConnection, ImmutableConnectionClient},
        mutable::connection::{MutableConnection, MutableConnectionClient},
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
    fidl_fuchsia_io::{
        NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MAX_FILENAME, OPEN_FLAG_CREATE_IF_ABSENT,
    },
    fuchsia_async::Channel,
    fuchsia_zircon::Status,
    parking_lot::Mutex,
    static_assertions::assert_eq_size,
    std::{
        clone::Clone,
        collections::{btree_map, BTreeMap},
        iter,
        marker::PhantomData,
        ops::DerefMut,
        sync::Arc,
    },
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

    fn get_entry(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        name: &str,
        path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, Status> {
        let mut this = self.inner.lock();

        match this.entries.get(name) {
            Some(entry) => {
                if flags & OPEN_FLAG_CREATE_IF_ABSENT != 0 {
                    return Err(Status::ALREADY_EXISTS);
                }

                Ok(entry.clone())
            }
            None => {
                let entry = Connection::entry_not_found(
                    scope.clone(),
                    self.clone(),
                    flags,
                    mode,
                    name,
                    path,
                )?;

                let _ = this.entries.insert(name.to_string(), entry.clone());
                Ok(entry)
            }
        }
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
        let (name, path_ref) = match path.next_with_ref() {
            (path_ref, Some(name)) => (name, path_ref),
            (_, None) => {
                // See comment above `Simple::mutable` as to why this selection is necessary.
                if self.mutable {
                    MutableConnection::<AlphabeticalTraversal>::create_connection(
                        scope,
                        self as Arc<dyn MutableConnectionClient<AlphabeticalTraversal>>,
                        flags,
                        mode,
                        server_end,
                    );
                } else {
                    ImmutableConnection::<AlphabeticalTraversal>::create_connection(
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

        // Do not hold the mutex more than necessary.  Plus, [`parking_lot::Mutex`] is not
        // re-entrant.  So we need to make sure to release the lock before we call `open()` is it
        // may turn out to be a recursive call, in case the directory contains itself directly or
        // through a number of other directories.  `get_entry` is responsible for locking `self`
        // and it will unlock it before returning.
        match self.get_entry(scope.clone(), flags, mode, name, path_ref) {
            Err(status) => send_on_open_with_error(flags, server_end, status),
            Ok(entry) => entry.open(scope, flags, mode, path, server_end),
        }
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

impl<Connection> EntryContainer for Simple<Connection>
where
    Connection: DerivedConnection<AlphabeticalTraversal> + 'static,
{
    fn get_entry(self: Arc<Self>, name: String) -> AsyncGetEntry {
        assert_eq_size!(u64, usize);
        if name.len() as u64 > MAX_FILENAME {
            return Status::INVALID_ARGS.into();
        }

        let this = self.inner.lock();
        match this.entries.get(&name) {
            Some(entry) => entry.clone().into(),
            None => Status::NOT_FOUND.into(),
        }
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

    fn link(self: Arc<Self>, name: String, entry: Arc<dyn DirectoryEntry>) -> Result<(), Status> {
        if name.len() as u64 >= MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        let mut this = self.inner.lock();

        this.watchers.send_event(&mut SingleNameEventProducer::added(&name));

        let _ = this.entries.insert(name, entry);
        Ok(())
    }

    unsafe fn rename_from(
        self: Arc<Self>,
        src: String,
        to: Box<dyn FnOnce(Arc<dyn DirectoryEntry>) -> Result<(), Status>>,
    ) -> Result<(), Status> {
        if src.len() as u64 >= MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        let mut this = self.inner.lock();

        let Inner { entries, watchers, .. } = this.deref_mut();

        let map_entry = match entries.entry(src.clone()) {
            btree_map::Entry::Vacant(_) => return Err(Status::NOT_FOUND),
            btree_map::Entry::Occupied(map_entry) => map_entry,
        };

        to(map_entry.get().clone())?;

        watchers.send_event(&mut SingleNameEventProducer::removed(&src));

        let _ = map_entry.remove();
        Ok(())
    }

    unsafe fn rename_to(
        self: Arc<Self>,
        dst: String,
        from: Box<dyn FnOnce() -> Result<Arc<dyn DirectoryEntry>, Status>>,
    ) -> Result<(), Status> {
        if dst.len() as u64 >= MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        let mut this = self.inner.lock();

        let entry = from()?;

        this.watchers.send_event(&mut SingleNameEventProducer::added(&dst));

        let _ = this.entries.insert(dst, entry);
        Ok(())
    }

    fn rename_within(self: Arc<Self>, src: String, dst: String) -> Result<(), Status> {
        if src.len() as u64 >= MAX_FILENAME || dst.len() as u64 >= MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        let mut this = self.inner.lock();

        // I assume we should send these events even even when `src == dst`.  In practice, a
        // particular client may not be aware that the names match, but may still rely on the fact
        // that the events occur.
        //
        // Watcher protocol expects to produce messages that list only one type of event.  So
        // we will send two independent event messages, each with one name.
        this.watchers.send_event(&mut SingleNameEventProducer::removed(&src));
        this.watchers.send_event(&mut SingleNameEventProducer::added(&dst));

        // We acquire the lock first, as in case `src != dst`, we want to make sure that the
        // recipients of these events can not see the directory in the state before the update.  I
        // assume that `src == dst` is unlikely case, and for the sake of reduction of code
        // duplication we can lock and then immediately unlock.
        //
        // It also provides sequencing for the watchers, as they will always receive `removed`
        // followed by `added`, and there will be no interleaving event in-between.  I think it is
        // not super important, as the watchers should probably be prepared to deal with all kinds
        // of sequences anyways.
        if src == dst {
            return Ok(());
        }

        let entry = match this.entries.remove(&src) {
            None => return Err(Status::NOT_FOUND),
            Some(entry) => entry,
        };

        let _ = this.entries.insert(dst, entry);
        Ok(())
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
