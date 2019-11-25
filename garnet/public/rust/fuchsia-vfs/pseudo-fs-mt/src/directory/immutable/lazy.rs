// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of a "lazy" pseudo directory.  See [`Lazy`] for details.

#[cfg(test)]
mod tests;

mod watchers_task;

use crate::{
    common::send_on_open_with_error,
    directory::{
        connection::DerivedConnection,
        dirents_sink,
        entry::{DirectoryEntry, EntryInfo},
        entry_container::{self, AsyncReadDirents, EntryContainer},
        immutable::connection::ImmutableConnection,
    },
    execution_scope::ExecutionScope,
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN},
    fuchsia_async::Channel,
    fuchsia_zircon::Status,
    futures::{
        channel::mpsc::{self, UnboundedSender},
        future::Future,
        stream::Stream,
    },
    std::{
        fmt::{self, Debug, Formatter},
        marker::PhantomData,
        sync::Arc,
    },
};

/// Events that can be sent over the watcher notifications stream.  See `watcher_events` argument
/// documentation of the [`lazy`] constructor.
#[derive(Debug)]
pub enum WatcherEvent {
    /// A directory itself has been removed.  Not all files systems will be able to support this
    /// event..  All the currently attached watchers will receive a WATCH_EVENT_DELETED event.
    /// `name` is the name of the directory in it's parent listing.
    Deleted(String),
    /// One or more entries have been added to the directory.  All the currently attached watchers
    /// will receive a WATCH_EVENT_ADDED event.
    Added(Vec<String>),
    /// One or more entries have been removed from the directory.  All the currently attached
    /// watchers will receive a WATCH_EVENT_REMOVED event.
    Removed(Vec<String>),
}

pub type GetEntryNamesResult = Result<Box<dyn dirents_sink::Sealed>, Status>;

pub type GetEntryResult = Result<Arc<dyn DirectoryEntry>, Status>;

/// Creates a lazy directory, with no watcher stream attached.  Watchers will not be able to attach
/// to this directory.  See [`lazy_with_watchers`].
///
/// See [`Lazy`] for additional details.
pub fn lazy<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>(
    get_entry_names: GetEntryNames,
    get_entry: GetEntry,
) -> Arc<Lazy<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>>
where
    TraversalPosition: Default + Send + Sync + 'static,
    GetEntryNames: Fn(TraversalPosition, Box<dyn dirents_sink::Sink<TraversalPosition>>) -> AsyncGetEntryNames
        + Send
        + Sync
        + 'static,
    AsyncGetEntryNames: Future<Output = GetEntryNamesResult> + Send + 'static,
    GetEntry: Fn(String) -> AsyncGetEntry + Send + Sync + 'static,
    AsyncGetEntry: Future<Output = GetEntryResult> + Send + 'static,
{
    Lazy::new(get_entry_names, get_entry)
}

/// Creates a lazy directory that can support watchers.  In order to process events from the
/// `watcher_events` stream the directory needs an execution `scope`.
///
/// See [`Lazy`] for additional details.
pub fn lazy_with_watchers<
    TraversalPosition,
    GetEntryNames,
    AsyncGetEntryNames,
    GetEntry,
    AsyncGetEntry,
    WatcherEvents,
>(
    scope: ExecutionScope,
    get_entry_names: GetEntryNames,
    get_entry: GetEntry,
    watcher_events: WatcherEvents,
) -> Arc<Lazy<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>>
where
    TraversalPosition: Default + Send + Sync + 'static,
    GetEntryNames: Fn(TraversalPosition, Box<dyn dirents_sink::Sink<TraversalPosition>>) -> AsyncGetEntryNames
        + Send
        + Sync
        + 'static,
    AsyncGetEntryNames: Future<Output = GetEntryNamesResult> + Send + 'static,
    GetEntry: Fn(String) -> AsyncGetEntry + Send + Sync + 'static,
    AsyncGetEntry: Future<Output = GetEntryResult> + Send + 'static,
    WatcherEvents: Stream<Item = WatcherEvent> + Send + 'static,
{
    Lazy::new_with_watchers(scope, get_entry_names, get_entry, watcher_events)
}

/// An implementation of a pseudo directory that generates nested entries only when they are
/// requested.  This could be useful when the number of entries is big and the expected use case is
/// that only a small fraction of all the entries will be interacted with at an given time.
///
/// [`lazy`], and [`lazy_with_watchers`] are used to construct lazy directories.
///
/// A lazy directory contains two callbacks and a stream.  One callback, called `get_entry_names`,
/// which is used when a directory listing is requested.  Another callback, called `get_entry`, is
/// used to construct and actual entry when it is accessed.  A stream, called `watcher_events` is
/// used to send notifications to the currently connected watchers.
///
/// `get_entry_names` is provided with a position and a sink.  The position allows the caller to
/// retrieve entry names starting at a point other then the very first entry.  The sink is use to
/// consume entry names and it may not be able to consume the whole directory content at once as it
/// is backed by a limited size buffer.  "sink" will remember the last provided possition, allowing
/// the caller to resume the list operation.  See [`traversal_position::AlphabeticalTraversal`] for
/// an example of a type designed to be used as a traversal position.
///
/// `get_entry` is expected to return a reference to a [`DirectoryEntry`] instance backing an
/// individual entry.  Notice that currently there is no caching or sharing of entry objects.
/// Every new `Open()` request will cause new entry object to be allocated and used.  See #ZX-3631
/// for the caching policy discussion.
///
/// NOTE There might be an alternative design, where `get_entry_names` returns a stream of entry
/// names information.  The connection object will hold on to this stream, using it to populate
/// `ReadDirents` requests and destroying the stream when `Rewind` is called.
///
/// `watcher_events` is a stream of events that when occure are forwarded to all the connected
/// watchers.  They are values of type [`WatcherEvent`].  If this stream reaches it's end existing
/// watcher connections will be closed and any new watchers will not be able to connect to the node
/// - they will receive a NOT_SUPPORTED error.
pub struct Lazy<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>
where
    TraversalPosition: Default + Send + Sync + 'static,
    GetEntryNames: Fn(TraversalPosition, Box<dyn dirents_sink::Sink<TraversalPosition>>) -> AsyncGetEntryNames
        + Send
        + Sync
        + 'static,
    AsyncGetEntryNames: Future<Output = GetEntryNamesResult> + Send + 'static,
    GetEntry: Fn(String) -> AsyncGetEntry + Send + Sync + 'static,
    AsyncGetEntry: Future<Output = GetEntryResult> + Send + 'static,
{
    /// This callback is invoked to get names of the entries inside the directory.  The first
    /// argument specifies the starting point.  The second argument is a "sink" that is used to
    /// collect entry names and their attributes.
    ///
    /// The first argument allows traversal to resume when there are more entries in the directory
    /// that can fit in one `Directory::ReadDirents` `io.fidl` request.  `TraversalPosition` is
    /// used to allow different directories to use different state to track the traveral progress.
    /// Different directories may also list entries in different order - alphabetical, insertion
    /// order, random.
    get_entry_names: GetEntryNames,

    /// This callback is invoked to get an actual directory entry object that corresponds to the
    /// specified name.  Note that this is never going to be called with ".", but otherwise might
    /// be called with names that has not been necessarily returned by the get_entry_names()
    /// callback.
    get_entry: GetEntry,

    watchers: UnboundedSender<WatcherCommand>,

    // TODO For some reason I need to have PhantomData fields for TraversalPosition and
    // AsyncGetEntryNames.  But not for AsyncGetEntry?  This is quite confusing - possibly a bug in
    // the compiler?  I would not expect the compiler to require a PhantomData for any of these
    // types.
    _traversal_position: PhantomData<TraversalPosition>,
    _async_get_entry_names: PhantomData<fn() -> AsyncGetEntryNames>,
}

enum WatcherCommand {
    RegisterWatcher { scope: ExecutionScope, mask: u32, channel: Channel },
    UnregisterWatcher { key: usize },
}

impl Debug for WatcherCommand {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        match self {
            WatcherCommand::RegisterWatcher { scope: _, mask, channel } => f
                .debug_struct("WatcherCommand::RegisterWatcher")
                .field("scope", &format_args!("_"))
                .field("mask", &mask)
                .field("channel", &channel)
                .finish(),
            WatcherCommand::UnregisterWatcher { key } => {
                f.debug_struct("WatcherCommand::UnregisterWatcher").field("key", &key).finish()
            }
        }
    }
}

impl<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>
    Lazy<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>
where
    TraversalPosition: Default + Send + Sync + 'static,
    GetEntryNames: Fn(TraversalPosition, Box<dyn dirents_sink::Sink<TraversalPosition>>) -> AsyncGetEntryNames
        + Send
        + Sync
        + 'static,
    AsyncGetEntryNames: Future<Output = GetEntryNamesResult> + Send + 'static,
    GetEntry: Fn(String) -> AsyncGetEntry + Send + Sync + 'static,
    AsyncGetEntry: Future<Output = GetEntryResult> + Send + 'static,
{
    fn new(get_entry_names: GetEntryNames, get_entry: GetEntry) -> Arc<Self> {
        // We will create a channel that would be immediately closed, as we need a sender even when
        // no watcher support is present.
        let (command_sender, _) = mpsc::unbounded();

        Arc::new(Lazy {
            get_entry_names,
            get_entry,
            watchers: command_sender,
            _traversal_position: PhantomData,
            _async_get_entry_names: PhantomData,
        })
    }

    fn new_with_watchers<WatcherEvents>(
        scope: ExecutionScope,
        get_entry_names: GetEntryNames,
        get_entry: GetEntry,
        watcher_events: WatcherEvents,
    ) -> Arc<Self>
    where
        WatcherEvents: Stream<Item = WatcherEvent> + Send + 'static,
    {
        let (command_sender, command_receiver) = mpsc::unbounded();

        let dir = Arc::new(Lazy {
            get_entry_names,
            get_entry,
            watchers: command_sender,
            _traversal_position: PhantomData,
            _async_get_entry_names: PhantomData,
        });

        let task = watchers_task::run(dir.clone(), command_receiver, watcher_events);

        // If we failed to start the watchers task, `command_receiver` will be closed and the
        // directory will effectivly stop supporting watchers.  This should only happen if the
        // executor is shutting down.  There is nothing useful we can do with this error.
        let _ = scope.spawn(task);

        dir
    }
}

impl<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry> DirectoryEntry
    for Lazy<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>
where
    TraversalPosition: Default + Send + Sync + 'static,
    GetEntryNames: Fn(TraversalPosition, Box<dyn dirents_sink::Sink<TraversalPosition>>) -> AsyncGetEntryNames
        + Send
        + Sync
        + 'static,
    AsyncGetEntryNames: Future<Output = GetEntryNamesResult> + Send + 'static,
    GetEntry: Fn(String) -> AsyncGetEntry + Send + Sync + 'static,
    AsyncGetEntry: Future<Output = GetEntryResult> + Send + 'static,
{
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        mut path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let name = match path.next() {
            Some(name) => name.to_string(),
            None => {
                ImmutableConnection::<TraversalPosition>::create_connection(
                    scope, self, flags, mode, server_end,
                );
                return;
            }
        };

        let task = Box::pin({
            let scope = scope.clone();
            async move {
                match (self.get_entry)(name).await {
                    Ok(entry) => entry.open(scope, flags, mode, path, server_end),
                    Err(status) => send_on_open_with_error(flags, server_end, status),
                }
            }
        });
        // Failure to spawn the task will just close the `server_end`.  As it is already gone there
        // is nothing we can do here.
        let _ = scope.spawn(task);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

impl<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry> EntryContainer
    for Lazy<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>
where
    TraversalPosition: Default + Send + Sync + 'static,
    GetEntryNames: Fn(TraversalPosition, Box<dyn dirents_sink::Sink<TraversalPosition>>) -> AsyncGetEntryNames
        + Send
        + Sync
        + 'static,
    AsyncGetEntryNames: Future<Output = GetEntryNamesResult> + Send + 'static,
    GetEntry: Fn(String) -> AsyncGetEntry + Send + Sync + 'static,
    AsyncGetEntry: Future<Output = GetEntryResult> + Send + 'static,
{
    fn get_entry(self: Arc<Self>, name: String) -> entry_container::AsyncGetEntry {
        // Can not use `into()` here.  Could not find a good `From` definition to be provided in
        // `directory/entry_container.rs` so that just a plain `.into()` would work here.
        let task = (self.get_entry)(name);
        entry_container::AsyncGetEntry::Future(Box::pin(task))
    }
}

impl<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>
    entry_container::Observable<TraversalPosition>
    for Lazy<TraversalPosition, GetEntryNames, AsyncGetEntryNames, GetEntry, AsyncGetEntry>
where
    TraversalPosition: Default + Send + Sync + 'static,
    GetEntryNames: Fn(TraversalPosition, Box<dyn dirents_sink::Sink<TraversalPosition>>) -> AsyncGetEntryNames
        + Send
        + Sync
        + 'static,
    AsyncGetEntryNames: Future<Output = GetEntryNamesResult> + Send + 'static,
    GetEntry: Fn(String) -> AsyncGetEntry + Send + Sync + 'static,
    AsyncGetEntry: Future<Output = GetEntryResult> + Send + 'static,
{
    fn read_dirents(
        self: Arc<Self>,
        pos: TraversalPosition,
        sink: Box<dyn dirents_sink::Sink<TraversalPosition>>,
    ) -> AsyncReadDirents {
        AsyncReadDirents::Future(Box::pin((self.get_entry_names)(pos, sink)))
    }

    fn register_watcher(
        self: Arc<Self>,
        scope: ExecutionScope,
        mask: u32,
        channel: Channel,
    ) -> Status {
        // Failure to send a command may indicate that the directory does not support watchers, or
        // that the executor shutdown is in progress.  In any case the error can be ignored.
        self.watchers
            .unbounded_send(WatcherCommand::RegisterWatcher { scope, mask, channel })
            .map(|()| Status::OK)
            .unwrap_or(Status::NOT_SUPPORTED)
    }

    fn unregister_watcher(self: Arc<Self>, key: usize) {
        // Failure to send a command may indicate that the directory does not support watchers, or
        // that the executor shutdown is in progress.  In any case the error can be ignored.
        let _ = self.watchers.unbounded_send(WatcherCommand::UnregisterWatcher { key });
    }
}
