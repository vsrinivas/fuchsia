// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of a "simple" pseudo directory.  See [`Simple`] for details.

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error},
    directory::{
        common::{check_child_connection_flags, encode_dirent, validate_and_split_path},
        connection::DirectoryConnection,
        controllable::Controllable,
        entry::{DirectoryEntry, EntryInfo},
        traversal_position::AlphabeticalTraversal,
        watchers::{Watchers, WatchersAddError, WatchersSendError},
        DEFAULT_DIRECTORY_PROTECTION_ATTRIBUTES,
    },
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        DirectoryObject, DirectoryRequest, NodeAttributes, NodeInfo, NodeMarker,
        DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MAX_FILENAME, MODE_TYPE_DIRECTORY, WATCH_EVENT_ADDED,
        WATCH_EVENT_REMOVED, WATCH_MASK_ADDED, WATCH_MASK_REMOVED,
    },
    fuchsia_async::Channel,
    fuchsia_zircon::{
        sys::{ZX_ERR_INVALID_ARGS, ZX_ERR_NOT_SUPPORTED, ZX_OK},
        Status,
    },
    futures::{
        future::{FusedFuture, FutureExt},
        stream::{FusedStream, FuturesUnordered, StreamExt, StreamFuture},
    },
    static_assertions::assert_eq_size,
    std::{
        collections::BTreeMap,
        future::Future,
        iter,
        marker::Unpin,
        ops::Bound,
        pin::Pin,
        task::{Context, Poll},
    },
    void::Void,
};

/// Creates an empty directory.
///
/// POSIX access attributes are set to [`DEFAULT_DIRECTORY_PROTECTION_ATTRIBUTES`].
pub fn empty<'entries>() -> Simple<'entries> {
    empty_attr(DEFAULT_DIRECTORY_PROTECTION_ATTRIBUTES)
}

/// Creates an empty directory with the specified POSIX access attributes.
pub fn empty_attr<'entries>(protection_attributes: u32) -> Simple<'entries> {
    Simple {
        protection_attributes,
        entries: BTreeMap::new(),
        connections: FuturesUnordered::new(),
        watchers: Watchers::new(),
    }
}

/// An implementation of a pseudo directory.  Most clients will probably just use the
/// DirectoryEntry trait to deal with the pseudo directories uniformly.
///
/// In this implementation pseudo directories own all the entries that are directly direct
/// children, also "running" direct entries when the directory itself is run via [`Future::poll`].
/// See [`DirectoryEntry`] documentation for details.
pub struct Simple<'entries> {
    /// MODE_PROTECTION_MASK attributes returned by this directory through io.fidl:Node::GetAttr.
    /// They have no meaning for the directory operation itself, but may have consequences to the
    /// POSIX emulation layer.  This field should only have set bits in the MODE_PROTECTION_MASK
    /// part.
    protection_attributes: u32,

    entries: BTreeMap<String, Box<dyn DirectoryEntry + 'entries>>,

    connections: FuturesUnordered<StreamFuture<SimpleDirectoryConnection>>,

    watchers: Watchers,
}

/// Return type for Simple::handle_request().
struct HandleRequestResult {
    /// Current connection state.
    connection_state: ConnectionState,

    /// If the command may have an effect on the children we will need to make sure we execute
    /// their `poll` method after processing this command.
    may_affect_children: bool,
}

#[derive(PartialEq, Eq)]
enum ConnectionState {
    Alive,
    Closed,
}

/// When in a "simple" directory is traversed, entries are returned in an alphanumeric order.
type SimpleDirectoryConnection = DirectoryConnection<AlphabeticalTraversal>;

impl<'entries> Simple<'entries> {
    /// Adds a child entry to this directory.  The directory will own the child entry item and will
    /// run it as part of the directory own `poll()` invocation.
    ///
    /// In case of any error new entry returned along with the status code.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    pub fn add_entry<DE>(
        &mut self,
        name: &str,
        entry: DE,
    ) -> Result<(), (Status, Box<dyn DirectoryEntry + 'entries>)>
    where
        DE: DirectoryEntry + 'entries,
    {
        self.add_boxed_entry(name, Box::new(entry))
    }

    pub fn add_boxed_entry(
        &mut self,
        name: &str,
        entry: Box<dyn DirectoryEntry + 'entries>,
    ) -> Result<(), (Status, Box<dyn DirectoryEntry + 'entries>)> {
        assert_eq_size!(u64, usize);
        if name.len() as u64 >= MAX_FILENAME {
            return Err((Status::INVALID_ARGS, entry));
        }

        if self.entries.contains_key(name) {
            return Err((Status::ALREADY_EXISTS, entry));
        }

        self.watchers.send_event(WATCH_MASK_ADDED, WATCH_EVENT_ADDED, name).unwrap_or_else(|err| {
            match err {
                WatchersSendError::NameTooLong => {
                    panic!("We just checked the length of the `name`.  There should be a bug.")
                }
            }
        });
        let _ = self.entries.insert(name.to_string(), entry);
        Ok(())
    }

    /// Attaches a new connection (`server_end`) to this object.  Any error are reported as
    /// `OnOpen` events on the `server_end` itself.
    fn add_connection(&mut self, flags: u32, mode: u32, server_end: ServerEnd<NodeMarker>) {
        if let Some(conn) = SimpleDirectoryConnection::connect(flags, mode, server_end) {
            self.connections.push(conn);
        }
    }

    // TODO(fxbug.dev/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    fn handle_request(
        &mut self,
        req: DirectoryRequest,
        connection: &mut SimpleDirectoryConnection,
    ) -> Result<HandleRequestResult, anyhow::Error> {
        let mut may_affect_children = false;

        match req {
            DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                self.handle_clone(connection.flags, flags, 0, object);
            }
            DirectoryRequest::Close { responder } => {
                responder.send(ZX_OK)?;
                return Ok(HandleRequestResult {
                    connection_state: ConnectionState::Closed,
                    may_affect_children,
                });
            }
            DirectoryRequest::Describe { responder } => {
                let mut info = NodeInfo::Directory(DirectoryObject);
                responder.send(&mut info)?;
            }
            DirectoryRequest::Sync { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::GetAttr { responder } => {
                let mut attrs = NodeAttributes {
                    mode: MODE_TYPE_DIRECTORY | self.protection_attributes,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                };
                responder.send(ZX_OK, &mut attrs)?;
            }
            DirectoryRequest::SetAttr { flags: _, attributes: _, responder } => {
                // According to zircon/system/fidl/fuchsia-io/io.fidl the only flag that might be
                // modified through this call is OPEN_FLAG_APPEND, and it is not supported by a
                // Simple directory.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::Open { flags, mode, path, object, control_handle: _ } => {
                self.handle_open(connection.flags, flags, mode, &path, object);
                // As we optimize our `Open` requests by navigating multiple path components at
                // once, we may attach a connected to a child node.
                may_affect_children = true;
            }
            DirectoryRequest::Unlink { path: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::ReadDirents { max_bytes, responder } => {
                self.handle_read_dirents(connection, max_bytes, |status, entries| {
                    responder.send(status.into_raw(), entries)
                })?;
            }
            DirectoryRequest::Rewind { responder } => {
                connection.seek = Default::default();
                responder.send(ZX_OK)?;
            }
            DirectoryRequest::GetToken { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED, None)?;
            }
            DirectoryRequest::Rename { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::Link { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::Watch { mask, options, watcher, responder } => {
                if options != 0 {
                    responder.send(ZX_ERR_INVALID_ARGS)?;
                } else {
                    let channel = Channel::from_channel(watcher)?;

                    let names = self.entries.keys();
                    let dot = ".".to_string();
                    let mut entries = iter::once(&dot).chain(names);
                    let status = self
                        .watchers
                        .add(&mut entries, mask, channel)
                        .map(|()| Status::OK)
                        .unwrap_or_else(|err| match err {
                            WatchersAddError::NameTooLong => panic!(
                                "All the names in 'entries' are checked to be within limits.  There \
                                is a bug somewhere."
                            ),
                            WatchersAddError::FIDL(_) => Status::IO_REFUSED,
                        });
                    responder.send(status.into_raw())?;
                }
            }
            _ => {}
        }
        Ok(HandleRequestResult { connection_state: ConnectionState::Alive, may_affect_children })
    }

    fn handle_clone(
        &mut self,
        parent_flags: u32,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let flags = match inherit_rights_for_clone(parent_flags, flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        self.add_connection(flags, mode, server_end);
    }

    fn handle_open(
        &mut self,
        parent_flags: u32,
        flags: u32,
        mut mode: u32,
        path: &str,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if path == "/" || path == "" {
            send_on_open_with_error(flags, server_end, Status::BAD_PATH);
            return;
        }

        if path == "." || path == "./" {
            self.handle_clone(parent_flags, flags, mode, server_end);
            return;
        }

        let (mut names, is_dir) = match validate_and_split_path(path) {
            Ok(v) => v,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        if is_dir {
            mode |= MODE_TYPE_DIRECTORY;
        }

        let flags = match check_child_connection_flags(parent_flags, flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        // It is up to the open method to handle OPEN_FLAG_DESCRIBE from this point on.
        self.open(flags, mode, &mut names, server_end);
    }

    fn handle_read_dirents<R>(
        &mut self,
        connection: &mut SimpleDirectoryConnection,
        max_bytes: u64,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, &[u8]) -> Result<(), fidl::Error>,
    {
        let mut buf = Vec::new();
        let mut fit_one = false;

        let (entries_iter, mut last_returned) = match &connection.seek {
            AlphabeticalTraversal::Start => {
                if !encode_dirent(
                    &mut buf,
                    max_bytes,
                    &EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                    ".",
                ) {
                    return responder(Status::BUFFER_TOO_SMALL, &buf);
                }

                fit_one = true;
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
                // pointing to "range".  Same for two the other "range()" invocations below.
                (self.entries.range::<String, _>(..), AlphabeticalTraversal::Dot)
            }

            AlphabeticalTraversal::Dot => {
                (self.entries.range::<String, _>(..), AlphabeticalTraversal::Dot)
            }

            AlphabeticalTraversal::Name(last_returned_name) => (
                self.entries
                    .range::<String, _>((Bound::Excluded(last_returned_name), Bound::Unbounded)),
                connection.seek.clone(),
            ),

            AlphabeticalTraversal::End => {
                return responder(Status::OK, &buf);
            }
        };

        for (name, entry) in entries_iter {
            if !encode_dirent(&mut buf, max_bytes, &entry.entry_info(), name) {
                connection.seek = last_returned;
                return responder(
                    if fit_one { Status::OK } else { Status::BUFFER_TOO_SMALL },
                    &buf,
                );
            }
            fit_one = true;
            last_returned = AlphabeticalTraversal::Name(name.clone());
        }

        connection.seek = AlphabeticalTraversal::End;
        return responder(Status::OK, &buf);
    }

    fn poll_entries(&mut self, cx: &mut Context<'_>) {
        for (name, entry) in self.entries.iter_mut() {
            match entry.poll_unpin(cx) {
                Poll::Ready(result) => {
                    panic!(
                        "Entry futures in a pseudo directory should never complete.\n\
                         Entry name: {}\n\
                         Result: {:#?}",
                        name, result
                    );
                }
                Poll::Pending => (),
            }
        }
    }

    fn poll_connections(&mut self, cx: &mut Context<'_>) -> bool {
        // In case a command may establish a connection to a child node we need to make sure to run
        // the child node `poll` method as well to allow the new connection to register itself in
        // the context.
        let mut rerun_children = false;

        // This loop is needed to make sure we do not miss any activations of the futures we are
        // managing.  For example, if a stream was activated and has several outstanding items, if
        // we do not loop, we would only process the first item and then exit the `poll` method.
        // And we would leave item(s) in the stream and would not process them until the next
        // activation due to another item been added.  So we need to poll the stream while we
        // receive Poll::Pending.
        //
        // This approach has a downside, as we do not give up control until if we have more items
        // coming in.  Ideally we would want to check if there is anything else left in the stream
        // and just set the waker to activate us again, giving the executor (and other futures
        // sharing this task) to do work.  Unfortunately, Stream does not provide an ability to see
        // if there are any items pending.
        loop {
            match self.connections.poll_next_unpin(cx) {
                Poll::Ready(Some((maybe_request, mut connection))) => {
                    if let Some(Ok(request)) = maybe_request {
                        match self.handle_request(request, &mut connection) {
                            Ok(HandleRequestResult { connection_state, may_affect_children }) => {
                                rerun_children |= may_affect_children;
                                if connection_state == ConnectionState::Alive {
                                    self.connections.push(connection.into_future());
                                }
                            }
                            // An error occurred while processing a request.  We will just close
                            // the connection, effectively closing the underlying channel in the
                            // destructor.
                            _ => (),
                        }
                    }
                    // Similarly to the error that occurs while handing a FIDL request, any
                    // connection level errors cause the connection to be closed.
                }
                // Even when we have no connections any more we still report Pending state, as we
                // may get more connections open in the future.  We will return Poll::Pending
                // below.  Getting any of these two values means that we have processed all the
                // items that might have been triggered current waker activation.
                Poll::Ready(None) | Poll::Pending => break,
            }
        }

        rerun_children
    }
}

impl<'entries> DirectoryEntry for Simple<'entries> {
    fn open(
        &mut self,
        flags: u32,
        mode: u32,
        path: &mut dyn Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let name = match path.next() {
            Some(name) => name,
            None => {
                self.add_connection(flags, mode, server_end);
                return;
            }
        };

        let entry = match self.entries.get_mut(name) {
            Some(entry) => entry,
            None => {
                send_on_open_with_error(flags, server_end, Status::NOT_FOUND);
                return;
            }
        };

        // While this function is recursive, and Rust does not support TCO at the moment, recursion
        // here does not seem to be too bad.  I've tested a method with a very similar layout:
        //
        //     fn open(&mut self, a: u32, b: u32, path: &mut Iterator<Item = &str>, v: u64) -> Result<(), Error>;
        //
        // You can run it here:
        //
        //     https://play.rust-lang.org/?version=nightly&gist=5471f93c52f3adb7c8d6741ea96f9bce
        //
        // Given a path with 2048 components, which is the maximum possible path, considering the
        // MAX_PATH restriction of 4096, the function used 290KBs of stack.  Rust, by default, uses
        // 2MB stacks.
        //
        // Considering that the open method will only use recursion for the pseudo directories
        // created by the server, it is not very likely that the server will create such a deep
        // tree in the first place.
        //
        // Removing recursion is a bit inconvenient, as open() is the API for the tree entries.
        // One way to remove the recursion that I can think of, is to introduce a
        //
        //     open_next_entry_or_consume(flags, mode, entry_name, path, server_end) -> Option<&mut DirectoryEntry>
        //
        // method that would either return the next DirectoryEntry or will consume the path further
        // down (recursively) returning None.  This would allow traversal to happen in a fixed
        // stack space, still allowing nodes like mount points to intercept the traversal process.
        // It seems like it will complicate the API for the DirectoryEntry implementations though.

        entry.open(flags, mode, path, server_end);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
}

impl<'entries> Controllable<'entries> for Simple<'entries> {
    fn add_boxed_entry(
        &mut self,
        name: &str,
        entry: Box<dyn DirectoryEntry + 'entries>,
    ) -> Result<(), (Status, Box<dyn DirectoryEntry + 'entries>)> {
        (self as &mut Simple<'_>).add_boxed_entry(name, entry)
    }

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    fn remove_entry(
        &mut self,
        name: &str,
    ) -> Result<Option<Box<dyn DirectoryEntry + 'entries>>, Status> {
        assert_eq_size!(u64, usize);
        if name.len() as u64 >= MAX_FILENAME {
            return Err(Status::INVALID_ARGS);
        }

        self.watchers.send_event(WATCH_MASK_REMOVED, WATCH_EVENT_REMOVED, name).unwrap_or_else(
            |err| match err {
                WatchersSendError::NameTooLong => {
                    panic!("We just checked the length of the `name`.  There should be a bug.")
                }
            },
        );
        Ok(self.entries.remove(name))
    }
}

impl<'entries> Unpin for Simple<'entries> {}

impl<'entries> Future for Simple<'entries> {
    type Output = Void;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        // NOTE Ordering here is important.  We need to give execution to all the child nodes
        // first, as if any of those nodes will somehow cause new connections to be added to the
        // `connections` list, we need to make sure we call `poll_next` at least once after a
        // connection has been added.  Otherwise we will return `Pending` and the context would not
        // be updated to include a waker for this new connection.  This can be observed in unit
        // tests where `run_until_stalled` will cause the test to be reported as stalled somewhere
        // in the middle.
        //
        // But as we optimize the way we open connections, an `Open` request may add connections to
        // child nodes directly, so we need to rerun `poll` for children in case of an `Open`
        // request.

        loop {
            self.poll_entries(cx);

            let rerun_children = self.poll_connections(cx);
            if !rerun_children {
                break;
            }
        }

        self.watchers.remove_dead(cx);

        Poll::Pending
    }
}

impl<'entries> FusedFuture for Simple<'entries> {
    fn is_terminated(&self) -> bool {
        for entry in self.entries.values() {
            if !entry.is_terminated() {
                return false;
            }
        }

        // If we have any watcher connections, we may still make progress when a watcher connection
        // is closed.
        !self.watchers.has_connections() && self.connections.is_terminated()
    }
}

#[cfg(test)]
mod tests {
    use super::{empty, empty_attr};

    use {
        crate::directory::test_utils::{
            run_server_client, run_server_client_with_open_requests_channel,
            DirentsSameInodeBuilder,
        },
        crate::file::simple::{read_only_static, read_write, write_only},
        crate::test_utils::open_get_proxy,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_io::{
            DirectoryEvent, DirectoryMarker, DirectoryObject, DirectoryProxy, FileEvent,
            FileMarker, NodeAttributes, NodeInfo, DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE,
            INO_UNKNOWN, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_POSIX, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
            WATCH_MASK_ADDED, WATCH_MASK_EXISTING, WATCH_MASK_IDLE, WATCH_MASK_REMOVED,
        },
        fuchsia_zircon::{sys::ZX_OK, Status},
        futures::SinkExt,
        libc::{S_IRGRP, S_IROTH, S_IRUSR, S_IXGRP, S_IXOTH, S_IXUSR},
        proc_macro_hack::proc_macro_hack,
        std::iter,
        std::sync::atomic::{AtomicUsize, Ordering},
    };

    // Create level import of this macro does not affect nested modules.  And as attributes can
    // only be applied to the whole "use" directive, this need to be present here and need to be
    // separate form the above.  "use crate::pseudo_directory" generates a warning referring to
    // "issue #52234 <https://github.com/rust-lang/rust/issues/52234>".
    #[proc_macro_hack(support_nested)]
    use fuchsia_vfs_pseudo_fs_macros::pseudo_directory;

    #[test]
    fn empty_directory() {
        run_server_client(OPEN_RIGHT_READABLE, empty(), |proxy| async move {
            assert_close!(proxy);
        });
    }

    #[test]
    fn empty_directory_get_attr() {
        run_server_client(OPEN_RIGHT_READABLE, empty(), |proxy| async move {
            assert_get_attr!(
                proxy,
                NodeAttributes {
                    mode: MODE_TYPE_DIRECTORY | S_IRUSR,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                }
            );
            assert_close!(proxy);
        });
    }

    #[test]
    fn empty_attr_directory_get_attr() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            empty_attr(S_IXOTH | S_IROTH | S_IXGRP | S_IRGRP | S_IXUSR | S_IRUSR),
            |proxy| async move {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_DIRECTORY
                            | (S_IXOTH | S_IROTH | S_IXGRP | S_IRGRP | S_IXUSR | S_IRUSR),
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn empty_directory_describe() {
        run_server_client(OPEN_RIGHT_READABLE, empty(), |proxy| async move {
            assert_describe!(proxy, NodeInfo::Directory(DirectoryObject));
            assert_close!(proxy);
        });
    }

    #[test]
    fn open_empty_directory_with_describe() {
        run_server_client_with_open_requests_channel(empty(), |mut open_sender| async move {
            let (proxy, server_end) =
                create_proxy::<DirectoryMarker>().expect("Failed to create connection endpoints");

            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_sender.send((flags, 0, Box::new(iter::empty()), server_end)).await.unwrap();
            assert_event!(proxy, DirectoryEvent::OnOpen_ { s, info }, {
                assert_eq!(s, ZX_OK);
                assert_eq!(info, Some(Box::new(NodeInfo::Directory(DirectoryObject))));
            });
        });
    }

    #[test]
    fn clone() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |first_proxy| async move {
            async fn assert_read_file(root: &DirectoryProxy) {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

                assert_read!(file, "Content");
                assert_close!(file);
            }

            assert_read_file(&first_proxy).await;

            let second_proxy = clone_get_directory_proxy_assert_ok!(
                &first_proxy,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE
            );

            assert_read_file(&second_proxy).await;

            assert_close!(first_proxy);
            assert_close!(second_proxy);
        });
    }

    #[test]
    fn clone_inherit_access() {
        use fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS;

        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |first_proxy| async move {
            async fn assert_read_file(root: &DirectoryProxy) {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

                assert_read!(file, "Content");
                assert_close!(file);
            }

            assert_read_file(&first_proxy).await;

            let second_proxy = clone_get_directory_proxy_assert_ok!(
                &first_proxy,
                CLONE_FLAG_SAME_RIGHTS | OPEN_FLAG_DESCRIBE
            );

            assert_read_file(&second_proxy).await;

            assert_close!(first_proxy);
            assert_close!(second_proxy);
        });
    }

    #[test]
    fn clone_cannot_increase_access() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            async fn assert_read_file(root: &DirectoryProxy) {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

                assert_read!(file, "Content");
                assert_close!(file);
            }

            assert_read_file(&root).await;

            clone_as_directory_assert_err!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
                Status::ACCESS_DENIED
            );

            assert_close!(root);
        });
    }

    #[test]
    fn clone_cannot_use_same_rights_flag_with_any_specific_right() {
        use fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS;

        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |proxy| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let file = open_get_file_proxy_assert_ok!(&proxy, flags, "file");

            assert_read!(file, "Content");
            assert_close!(file);

            clone_as_directory_assert_err!(
                &proxy,
                CLONE_FLAG_SAME_RIGHTS | OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                Status::INVALID_ARGS
            );

            assert_close!(proxy);
        });
    }

    #[test]
    fn one_file_open_existing() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

            assert_read!(file, "Content");
            assert_close!(file);

            assert_close!(root);
        });
    }

    #[test]
    fn one_file_open_missing() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_file_assert_err!(&root, flags, "file2", Status::NOT_FOUND);

            assert_close!(root);
        });
    }

    #[test]
    fn small_tree_traversal() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only_static("/dev/fs /"),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
            "uname" => read_only_static("Fuchsia"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            async fn open_read_close<'a>(
                from_dir: &'a DirectoryProxy,
                path: &'a str,
                expected_content: &'a str,
            ) {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&from_dir, flags, path);
                assert_read!(file, expected_content);
                assert_close!(file);
            }

            open_read_close(&root, "etc/fstab", "/dev/fs /").await;

            {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                open_read_close(&ssh_dir, "sshd_config", "# Empty").await;
            }

            open_read_close(&root, "etc/ssh/sshd_config", "# Empty").await;
            open_read_close(&root, "uname", "Fuchsia").await;

            assert_close!(root);
        });
    }

    #[test]
    fn open_writable_in_subdir() {
        let write_count = &AtomicUsize::new(0);
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_write(
                        || Ok(b"# Empty".to_vec()),
                        100,
                        |content| {
                            let count = write_count.fetch_add(1, Ordering::Relaxed);
                            assert_eq!(*&content, format!("Port {}", 22 + count).as_bytes());
                            Ok(())
                        }
                    )
                }
            }
        };

        run_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |root| async move {
            async fn open_read_write_close<'a>(
                from_dir: &'a DirectoryProxy,
                path: &'a str,
                expected_content: &'a str,
                new_content: &'a str,
                write_count: &'a AtomicUsize,
                expected_count: usize,
            ) {
                let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&from_dir, flags, path);
                assert_read!(file, expected_content);
                assert_seek!(file, 0, Start);
                assert_write!(file, new_content);
                assert_close!(file);

                assert_eq!(write_count.load(Ordering::Relaxed), expected_count);
            }

            {
                let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;
                let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                open_read_write_close(
                    &ssh_dir,
                    "sshd_config",
                    "# Empty",
                    "Port 22",
                    write_count,
                    1,
                )
                .await;
            }

            open_read_write_close(
                &root,
                "etc/ssh/sshd_config",
                "# Empty",
                "Port 23",
                write_count,
                2,
            )
            .await;

            assert_close!(root);
        });
    }

    #[test]
    fn open_write_only() {
        let write_count = &AtomicUsize::new(0);
        let root = pseudo_directory! {
            "dev" => pseudo_directory! {
                "output" => write_only(
                    100,
                    |content| {
                        let count = write_count.fetch_add(1, Ordering::Relaxed);
                        assert_eq!(*&content, format!("Message {}", 1 + count).as_bytes());
                        Ok(())
                    }
                )
            }
        };

        run_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |root| async move {
            async fn open_write_close<'a>(
                from_dir: &'a DirectoryProxy,
                new_content: &'a str,
                write_count: &'a AtomicUsize,
                expected_count: usize,
            ) {
                let flags = OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&from_dir, flags, "dev/output");
                assert_write!(file, new_content);
                assert_close!(file);

                assert_eq!(write_count.load(Ordering::Relaxed), expected_count);
            }

            open_write_close(&root, "Message 1", write_count, 1).await;
            open_write_close(&root, "Message 2", write_count, 2).await;

            assert_close!(root);
        });
    }

    #[test]
    fn open_non_existing_path() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "file1" => read_only_static("Content 1"),
            },
            "file2" => read_only_static("Content 2"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_file_assert_err!(&root, flags, "non-existing", Status::NOT_FOUND);
            open_as_file_assert_err!(&root, flags, "dir/file10", Status::NOT_FOUND);
            open_as_file_assert_err!(&root, flags, "dir/dir/file10", Status::NOT_FOUND);
            open_as_file_assert_err!(&root, flags, "dir/dir/file1", Status::NOT_FOUND);

            assert_close!(root);
        });
    }

    #[test]
    fn open_empty_path() {
        let root = pseudo_directory! {
            "file_foo" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_file_assert_err!(&root, flags, "", Status::BAD_PATH);

            assert_close!(root);
        });
    }

    #[test]
    fn open_path_within_a_file() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "file1" => read_only_static("Content 1"),
            },
            "file2" => read_only_static("Content 2"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_file_assert_err!(&root, flags, "file2/file1", Status::NOT_DIR);
            open_as_file_assert_err!(&root, flags, "dir/file1/file3", Status::NOT_DIR);

            assert_close!(root);
        });
    }

    #[test]
    fn open_file_as_directory() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "file1" => read_only_static("Content 1"),
            },
            "file2" => read_only_static("Content 2"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let mode = MODE_TYPE_DIRECTORY;
            {
                let proxy = open_get_proxy::<FileMarker>(&root, flags, mode, "file2");
                assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
                    assert_eq!(Status::from_raw(s), Status::NOT_DIR);
                    assert_eq!(info, None);
                });
            }
            {
                let proxy = open_get_proxy::<FileMarker>(&root, flags, mode, "dir/file1");
                assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
                    assert_eq!(Status::from_raw(s), Status::NOT_DIR);
                    assert_eq!(info, None);
                });
            }

            assert_close!(root);
        });
    }

    #[test]
    fn open_directory_as_file() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "dir2" => pseudo_directory! {},
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let mode = MODE_TYPE_FILE;
            {
                let proxy = open_get_proxy::<DirectoryMarker>(&root, flags, mode, "dir");
                assert_event!(proxy, DirectoryEvent::OnOpen_ { s, info }, {
                    assert_eq!(Status::from_raw(s), Status::NOT_FILE);
                    assert_eq!(info, None);
                });
            }
            {
                let proxy = open_get_proxy::<DirectoryMarker>(&root, flags, mode, "dir/dir2");
                assert_event!(proxy, DirectoryEvent::OnOpen_ { s, info }, {
                    assert_eq!(Status::from_raw(s), Status::NOT_FILE);
                    assert_eq!(info, None);
                });
            }

            assert_close!(root);
        });
    }

    #[test]
    fn trailing_slash_means_directory() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
            "dir" => pseudo_directory! {},
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

            open_as_file_assert_err!(&root, flags, "file/", Status::NOT_DIR);

            {
                let file = open_get_file_proxy_assert_ok!(&root, flags, "file");
                assert_read!(file, "Content");
                assert_close!(file);
            }

            {
                let sub_dir = open_get_directory_proxy_assert_ok!(&root, flags, "dir/");
                assert_close!(sub_dir);
            }

            assert_close!(root);
        });
    }

    #[test]
    fn no_dots_in_open() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
            "dir" => pseudo_directory! {
                "dir2" => pseudo_directory! {},
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_directory_assert_err!(&root, flags, "dir/../dir2", Status::INVALID_ARGS);
            open_as_directory_assert_err!(&root, flags, "dir/./dir2", Status::INVALID_ARGS);
            open_as_directory_assert_err!(&root, flags, "./dir", Status::INVALID_ARGS);

            assert_close!(root);
        });
    }

    #[test]
    fn no_consequtive_slashes_in_open() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "dir2" => pseudo_directory! {},
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_directory_assert_err!(&root, flags, "dir/../dir2", Status::INVALID_ARGS);
            open_as_directory_assert_err!(&root, flags, "dir/./dir2", Status::INVALID_ARGS);
            open_as_directory_assert_err!(&root, flags, "dir//dir2", Status::INVALID_ARGS);
            open_as_directory_assert_err!(&root, flags, "dir/dir2//", Status::INVALID_ARGS);
            open_as_directory_assert_err!(&root, flags, "//dir/dir2", Status::INVALID_ARGS);
            open_as_directory_assert_err!(&root, flags, "./dir", Status::INVALID_ARGS);

            assert_close!(root);
        });
    }

    #[test]
    fn directories_restrict_nested_read_permissions() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "file" => read_only_static("Content"),
            },
        };

        run_server_client(0, root, |root| async move {
            open_as_file_assert_err!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                "dir/file",
                Status::ACCESS_DENIED
            );

            assert_close!(root);
        });
    }

    #[test]
    fn directories_restrict_nested_write_permissions() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "file" => write_only(100, |_content| {
                    panic!("Access permissions should not allow this file to be written");
                }),
            },
        };

        run_server_client(0, root, |root| async move {
            open_as_file_assert_err!(
                &root,
                OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
                "dir/file",
                Status::ACCESS_DENIED
            );

            assert_close!(root);
        });
    }

    #[test]
    fn flag_posix_means_writable() {
        let write_count = &AtomicUsize::new(0);
        let root = pseudo_directory! {
            "nested" => pseudo_directory! {
                "file" => read_write(
                    || Ok(b"Content".to_vec()),
                    20,
                    |content| {
                        write_count.fetch_add(1, Ordering::Relaxed);
                        assert_eq!(*&content, b"New content");
                        Ok(())
                    }),
            },
        };

        run_server_client(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, root, |root| async move {
            let nested = open_get_directory_proxy_assert_ok!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX | OPEN_FLAG_DESCRIBE,
                "nested"
            );

            clone_get_directory_proxy_assert_ok!(
                &nested,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE
            );

            {
                let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&nested, flags, "file");

                assert_read!(file, "Content");
                assert_seek!(file, 0, Start);
                assert_write!(file, "New content");

                assert_close!(file);
            }

            assert_close!(nested);
            assert_close!(root);

            assert_eq!(write_count.load(Ordering::Relaxed), 1);
        });
    }

    #[test]
    fn flag_posix_does_not_add_writable_to_read_only() {
        let root = pseudo_directory! {
            "nested" => pseudo_directory! {
                "file" => read_write(
                    || Ok(b"Content".to_vec()),
                    100,
                    |_content| {
                        panic!("OPEN_FLAG_POSIX should not add OPEN_RIGHT_WRITABLE for directories");
                    }),
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let nested = open_get_directory_proxy_assert_ok!(
                &root,
                OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX | OPEN_FLAG_DESCRIBE,
                "nested"
            );

            clone_as_directory_assert_err!(
                &nested,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
                Status::ACCESS_DENIED
            );

            {
                let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;
                open_as_file_assert_err!(&nested, flags, "file", Status::ACCESS_DENIED);
            }

            {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&nested, flags, "file");

                assert_read!(file, "Content");
                assert_close!(file);
            }

            assert_close!(nested);
            assert_close!(root);
        });
    }

    #[test]
    fn read_dirents_large_buffer() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only_static("/dev/fs /"),
                "passwd" => read_only_static("[redacted]"),
                "shells" => read_only_static("/bin/bash"),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
            "files" => read_only_static("Content"),
            "more" => read_only_static("Content"),
            "uname" => read_only_static("Fuchsia"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            {
                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected
                    .add(DIRENT_TYPE_DIRECTORY, b".")
                    .add(DIRENT_TYPE_DIRECTORY, b"etc")
                    .add(DIRENT_TYPE_FILE, b"files")
                    .add(DIRENT_TYPE_FILE, b"more")
                    .add(DIRENT_TYPE_FILE, b"uname");

                assert_read_dirents!(root, 1000, expected.into_vec());
            }

            {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let etc_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc");

                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected
                    .add(DIRENT_TYPE_DIRECTORY, b".")
                    .add(DIRENT_TYPE_FILE, b"fstab")
                    .add(DIRENT_TYPE_FILE, b"passwd")
                    .add(DIRENT_TYPE_FILE, b"shells")
                    .add(DIRENT_TYPE_DIRECTORY, b"ssh");

                assert_read_dirents!(etc_dir, 1000, expected.into_vec());
                assert_close!(etc_dir);
            }

            {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"sshd_config");

                assert_read_dirents!(ssh_dir, 1000, expected.into_vec());
                assert_close!(ssh_dir);
            }

            assert_close!(root);
        });
    }

    #[test]
    fn read_dirents_small_buffer() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! { },
            "files" => read_only_static("Content"),
            "more" => read_only_static("Content"),
            "uname" => read_only_static("Fuchsia"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    // Entry header is 10 bytes + length of the name in bytes.
                    // (10 + 1) = 11
                    expected.add(DIRENT_TYPE_DIRECTORY, b".");
                    assert_read_dirents!(root, 11, expected.into_vec());
                }

                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected
                        // (10 + 3) = 13
                        .add(DIRENT_TYPE_DIRECTORY, b"etc")
                        // 13 + (10 + 5) = 28
                        .add(DIRENT_TYPE_FILE, b"files");
                    assert_read_dirents!(root, 28, expected.into_vec());
                }

                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected.add(DIRENT_TYPE_FILE, b"more").add(DIRENT_TYPE_FILE, b"uname");
                    assert_read_dirents!(root, 100, expected.into_vec());
                }

                assert_read_dirents!(root, 100, vec![]);
                assert_close!(root);
            }
        });
    }

    #[test]
    fn read_dirents_very_small_buffer() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                // Entry header is 10 bytes, so this read should not be able to return a single entry.
                assert_read_dirents_err!(root, 8, Status::BUFFER_TOO_SMALL);
                assert_close!(root);
            }
        });
    }

    #[test]
    fn read_dirents_rewind() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! { },
            "files" => read_only_static("Content"),
            "more" => read_only_static("Content"),
            "uname" => read_only_static("Fuchsia"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    // Entry header is 10 bytes + length of the name in bytes.
                    expected
                        // (10 + 1) = 11
                        .add(DIRENT_TYPE_DIRECTORY, b".")
                        // 11 + (10 + 3) = 24
                        .add(DIRENT_TYPE_DIRECTORY, b"etc")
                        // 24 + (10 + 5) = 39
                        .add(DIRENT_TYPE_FILE, b"files");
                    assert_read_dirents!(root, 39, expected.into_vec());
                }

                assert_rewind!(root);

                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    // Entry header is 10 bytes + length of the name in bytes.
                    expected
                        // (10 + 1) = 11
                        .add(DIRENT_TYPE_DIRECTORY, b".")
                        // 11 + (10 + 3) = 24
                        .add(DIRENT_TYPE_DIRECTORY, b"etc");
                    assert_read_dirents!(root, 24, expected.into_vec());
                }

                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected
                        .add(DIRENT_TYPE_FILE, b"files")
                        .add(DIRENT_TYPE_FILE, b"more")
                        .add(DIRENT_TYPE_FILE, b"uname");
                    assert_read_dirents!(root, 200, expected.into_vec());
                }

                assert_read_dirents!(root, 100, vec![]);
                assert_close!(root);
            }
        });
    }

    #[test]
    fn node_reference_ignores_read_access() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_READABLE,
            root,
            |root| async move {
                open_as_file_assert_err!(
                    &root,
                    OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                    "file",
                    Status::ACCESS_DENIED
                );

                clone_as_directory_assert_err!(
                    &root,
                    OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                    Status::ACCESS_DENIED
                );

                assert_close!(root);
            },
        );
    }

    #[test]
    fn node_reference_ignores_write_access() {
        let root = pseudo_directory! {
            "file" => read_only_static("Content"),
        };

        run_server_client(
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_NODE_REFERENCE,
            root,
            |root| async move {
                open_as_file_assert_err!(
                    &root,
                    OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
                    "file",
                    Status::ACCESS_DENIED
                );

                clone_as_directory_assert_err!(
                    &root,
                    OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
                    Status::ACCESS_DENIED
                );

                assert_close!(root);
            },
        );
    }

    #[test]
    fn node_reference_allows_read_dirents() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only_static("/dev/fs /"),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
            "files" => read_only_static("Content"),
        };

        run_server_client(OPEN_FLAG_NODE_REFERENCE, root, |root| async move {
            {
                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected
                    .add(DIRENT_TYPE_DIRECTORY, b".")
                    .add(DIRENT_TYPE_DIRECTORY, b"etc")
                    .add(DIRENT_TYPE_FILE, b"files");

                assert_read_dirents!(root, 1000, expected.into_vec());
            }

            {
                let flags = OPEN_FLAG_DESCRIBE;
                let etc_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc");

                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected
                    .add(DIRENT_TYPE_DIRECTORY, b".")
                    .add(DIRENT_TYPE_FILE, b"fstab")
                    .add(DIRENT_TYPE_DIRECTORY, b"ssh");

                assert_read_dirents!(etc_dir, 1000, expected.into_vec());
                assert_close!(etc_dir);
            }

            {
                let flags = OPEN_FLAG_DESCRIBE;
                let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"sshd_config");

                assert_read_dirents!(ssh_dir, 1000, expected.into_vec());
                assert_close!(ssh_dir);
            }

            assert_close!(root);
        });
    }

    #[test]
    fn watch_empty() {
        run_server_client(OPEN_RIGHT_READABLE, empty(), |root| async move {
            let mask =
                WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(watcher_client, { EXISTING, "." });
            assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

            drop(watcher_client);
            assert_close!(root);
        });
    }

    #[test]
    fn watch_non_empty() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only_static("/dev/fs /"),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
            "files" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let mask =
                WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(
                watcher_client,
                { EXISTING, "." },
                { EXISTING, "etc" },
                { EXISTING, "files" },
            );
            assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

            drop(watcher_client);
            assert_close!(root);
        });
    }

    #[test]
    fn watch_two_watchers() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only_static("/dev/fs /"),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
            "files" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let mask =
                WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher1_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(
                watcher1_client,
                { EXISTING, "." },
                { EXISTING, "etc" },
                { EXISTING, "files" },
            );
            assert_watcher_one_message_watched_events!(watcher1_client, { IDLE, vec![] });

            let watcher2_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(
                watcher2_client,
                { EXISTING, "." },
                { EXISTING, "etc" },
                { EXISTING, "files" },
            );
            assert_watcher_one_message_watched_events!(watcher2_client, { IDLE, vec![] });

            drop(watcher1_client);
            drop(watcher2_client);
            assert_close!(root);
        });
    }

    #[test]
    fn watch_with_mask() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only_static("/dev/fs /"),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
            "files" => read_only_static("Content"),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| async move {
            let mask = WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
            let watcher_client = assert_watch!(root, mask);

            assert_watcher_one_message_watched_events!(watcher_client, { IDLE, vec![] });

            drop(watcher_client);
            assert_close!(root);
        });
    }
}
