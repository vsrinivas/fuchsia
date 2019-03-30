// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of a "simple" pseudo directory.  See [`Simple`] for details.

use {
    crate::common::send_on_open_with_error,
    crate::directory::{
        common::{encode_dirent, validate_and_split_path},
        connection::DirectoryConnection,
        controllable::Controllable,
        entry::{DirectoryEntry, EntryInfo},
        traversal_position::AlphabeticalTraversal,
        watchers::{Watchers, WatchersAddError, WatchersSendError},
        DEFAULT_DIRECTORY_PROTECTION_ATTRIBUTES,
    },
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
        stream::{FuturesUnordered, StreamExt, StreamFuture},
        task::Waker,
        Future, Poll,
    },
    std::{
        collections::BTreeMap, iter, iter::ExactSizeIterator, marker::Unpin, ops::Bound, pin::Pin,
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

    entries: BTreeMap<String, Box<DirectoryEntry + 'entries>>,

    connections: FuturesUnordered<StreamFuture<SimpleDirectoryConnection>>,

    watchers: Watchers,
}

/// Return type for Simple::handle_request().
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
    ) -> Result<(), (Status, Box<DirectoryEntry + 'entries>)>
    where
        DE: DirectoryEntry + 'entries,
    {
        self.add_boxed_entry(name, Box::new(entry))
    }

    /// Attaches a new connection, client end `server_end`, to this object.  Any error are reported
    /// as `OnOpen` events on the `server_end` itself.
    fn add_connection(
        &mut self,
        parent_flags: u32,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if let Some(conn) =
            SimpleDirectoryConnection::connect(parent_flags, flags, mode, server_end)
        {
            self.connections.push(conn);
        }
    }

    fn handle_request(
        &mut self,
        req: DirectoryRequest,
        connection: &mut SimpleDirectoryConnection,
    ) -> Result<ConnectionState, failure::Error> {
        match req {
            DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                self.add_connection(connection.flags, flags, 0, object);
            }
            DirectoryRequest::Close { responder } => {
                responder.send(ZX_OK)?;
                return Ok(ConnectionState::Closed);
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
            DirectoryRequest::Ioctl { opcode: _, max_out: _, handles: _, in_: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED, &mut iter::empty(), &mut iter::empty())?;
            }
            DirectoryRequest::Open { flags, mode, path, object, control_handle: _ } => {
                self.handle_open(flags, mode, &path, object);
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

                    let mut names = self.entries.keys().map(|k| k.as_str());
                    let status = self
                        .watchers
                        .add(&mut names, mask, channel)
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
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_open(
        &mut self,
        flags: u32,
        mut mode: u32,
        path: &str,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if path == "/" {
            send_on_open_with_error(flags, server_end, Status::INVALID_ARGS);
            return;
        }

        if path == "." {
            self.open(flags, mode, &mut iter::empty(), server_end);
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
        R: FnOnce(Status, &mut ExactSizeIterator<Item = u8>) -> Result<(), fidl::Error>,
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
                    return responder(Status::BUFFER_TOO_SMALL, &mut buf.iter().cloned());
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
                return responder(Status::OK, &mut buf.iter().cloned());
            }
        };

        for (name, entry) in entries_iter {
            if !encode_dirent(&mut buf, max_bytes, &entry.entry_info(), name) {
                connection.seek = last_returned;
                return responder(
                    if fit_one { Status::OK } else { Status::BUFFER_TOO_SMALL },
                    &mut buf.iter().cloned(),
                );
            }
            fit_one = true;
            last_returned = AlphabeticalTraversal::Name(name.clone());
        }

        connection.seek = AlphabeticalTraversal::End;
        return responder(Status::OK, &mut buf.iter().cloned());
    }
}

impl<'entries> DirectoryEntry for Simple<'entries> {
    fn open(
        &mut self,
        flags: u32,
        mode: u32,
        path: &mut Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let name = match path.next() {
            Some(name) => name,
            None => {
                self.add_connection(!0, flags, mode, server_end);
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
        entry: Box<DirectoryEntry + 'entries>,
    ) -> Result<(), (Status, Box<DirectoryEntry + 'entries>)> {
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

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    fn remove_entry(
        &mut self,
        name: &str,
    ) -> Result<Option<Box<DirectoryEntry + 'entries>>, Status> {
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

    fn poll(mut self: Pin<&mut Self>, lw: &Waker) -> Poll<Self::Output> {
        loop {
            let mut did_work = false;

            match self.connections.poll_next_unpin(lw) {
                Poll::Ready(Some((maybe_request, mut connection))) => {
                    did_work = true;
                    if let Some(Ok(request)) = maybe_request {
                        match self.handle_request(request, &mut connection) {
                            Ok(ConnectionState::Alive) => {
                                self.connections.push(connection.into_future())
                            }
                            Ok(ConnectionState::Closed) => (),
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
                // below, if no other items did any work and we are existing our loop.
                Poll::Ready(None) | Poll::Pending => (),
            }

            for (name, entry) in self.entries.iter_mut() {
                match entry.poll_unpin(lw) {
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

            self.watchers.remove_dead(lw);

            if !did_work {
                break;
            }
        }

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
        //
        // As a pseudo directory blocks when no connections are available, it can not use
        // `connections.is_terminated()`.  `FuturesUnordered::is_terminated()` will return `false`
        // for an empty set of connections for the first time, while `Simple::poll()` will return
        // `Pending` in the same situation.  If we do not return `true` here for the empty
        // connections case for the first time instead, we will hang.
        !self.watchers.has_connections() && self.connections.len() == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use {
        crate::directory::test_utils::{
            run_server_client, run_server_client_with_open_requests_channel,
            DirentsSameInodeBuilder,
        },
        crate::file::simple::{read_only, read_write, write_only},
        crate::test_utils::open_get_proxy,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_io::{
            DirectoryEvent, DirectoryMarker, DirectoryObject, DirectoryProxy, FileEvent,
            FileMarker, FileObject, SeekOrigin, DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE,
            INO_UNKNOWN, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_NODE_REFERENCE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        futures::SinkExt,
        libc::{S_IRGRP, S_IROTH, S_IRUSR, S_IXGRP, S_IXOTH, S_IXUSR},
        proc_macro_hack::proc_macro_hack,
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
        run_server_client(OPEN_RIGHT_READABLE, empty(), async move |proxy| {
            assert_close!(proxy);
        });
    }

    #[test]
    fn empty_directory_get_attr() {
        run_server_client(OPEN_RIGHT_READABLE, empty(), async move |proxy| {
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
            async move |proxy| {
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
        run_server_client(OPEN_RIGHT_READABLE, empty(), async move |proxy| {
            assert_describe!(proxy, NodeInfo::Directory(DirectoryObject));
            assert_close!(proxy);
        });
    }

    #[test]
    fn open_empty_directory_with_describe() {
        run_server_client_with_open_requests_channel(empty(), async move |mut open_sender| {
            let (proxy, server_end) =
                create_proxy::<DirectoryMarker>().expect("Failed to create connection endpoints");

            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            await!(open_sender.send((flags, 0, Box::new(iter::empty()), server_end))).unwrap();
            assert_event!(proxy, DirectoryEvent::OnOpen_ { s, info }, {
                assert_eq!(s, ZX_OK);
                assert_eq!(info, Some(Box::new(NodeInfo::Directory(DirectoryObject))));
            });
        });
    }

    #[test]
    fn clone() {
        let root = pseudo_directory! {
            "file" => read_only(|| Ok(b"Content".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |first_proxy| {
            async fn assert_read_file(root: &DirectoryProxy) {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

                assert_read!(file, "Content");
                assert_close!(file);
            }

            await!(assert_read_file(&first_proxy));

            let second_proxy = clone_get_directory_proxy_assert_ok!(
                &first_proxy,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE
            );

            await!(assert_read_file(&second_proxy));

            assert_close!(first_proxy);
            assert_close!(second_proxy);
        });
    }

    #[test]
    fn clone_cannot_increase_access() {
        let root = pseudo_directory! {
            "file" => read_only(|| Ok(b"Content".to_vec())),
        };

        run_server_client(0, root, async move |first_proxy| {
            async fn assert_read_file(root: &DirectoryProxy) {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

                assert_read!(file, "Content");
                assert_close!(file);
            }

            await!(assert_read_file(&first_proxy));

            clone_as_directory_assert_err!(
                &first_proxy,
                OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
                Status::ACCESS_DENIED
            );

            assert_close!(first_proxy);
        });
    }

    #[test]
    fn one_file_open_existing() {
        let root = pseudo_directory! {
            "file" => read_only(|| Ok(b"Content".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
            "file" => read_only(|| Ok(b"Content".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_file_assert_err!(&root, flags, "file2", Status::NOT_FOUND);

            assert_close!(root);
        });
    }

    #[test]
    fn small_tree_traversal() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only(|| Ok(b"/dev/fs /".to_vec())),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only(|| Ok(b"# Empty".to_vec())),
                },
            },
            "uname" => read_only(|| Ok(b"Fuchsia".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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

            await!(open_read_close(&root, "etc/fstab", "/dev/fs /"));

            {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                await!(open_read_close(&ssh_dir, "sshd_config", "# Empty"));
            }

            await!(open_read_close(&root, "etc/ssh/sshd_config", "# Empty"));
            await!(open_read_close(&root, "uname", "Fuchsia"));

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

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                await!(open_read_write_close(
                    &ssh_dir,
                    "sshd_config",
                    "# Empty",
                    "Port 22",
                    write_count,
                    1
                ));
            }

            await!(open_read_write_close(
                &root,
                "etc/ssh/sshd_config",
                "# Empty",
                "Port 23",
                write_count,
                2
            ));

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

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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

            await!(open_write_close(&root, "Message 1", write_count, 1));
            await!(open_write_close(&root, "Message 2", write_count, 2));

            assert_close!(root);
        });
    }

    #[test]
    fn open_non_existing_path() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "file1" => read_only(|| Ok(b"Content 1".to_vec())),
            },
            "file2" => read_only(|| Ok(b"Content 2".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            open_as_file_assert_err!(&root, flags, "non-existing", Status::NOT_FOUND);
            open_as_file_assert_err!(&root, flags, "dir/file10", Status::NOT_FOUND);
            open_as_file_assert_err!(&root, flags, "dir/dir/file10", Status::NOT_FOUND);
            open_as_file_assert_err!(&root, flags, "dir/dir/file1", Status::NOT_FOUND);

            assert_close!(root);
        });
    }

    #[test]
    fn open_path_within_a_file() {
        let root = pseudo_directory! {
            "dir" => pseudo_directory! {
                "file1" => read_only(|| Ok(b"Content 1".to_vec())),
            },
            "file2" => read_only(|| Ok(b"Content 2".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
                "file1" => read_only(|| Ok(b"Content 1".to_vec())),
            },
            "file2" => read_only(|| Ok(b"Content 2".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
            "file" => read_only(|| Ok(b"Content".to_vec())),
            "dir" => pseudo_directory! {},
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
            "file" => read_only(|| Ok(b"Content".to_vec())),
            "dir" => pseudo_directory! {
                "dir2" => pseudo_directory! {},
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
    fn directories_do_not_restrict_write_permission() {
        let root = pseudo_directory! {
            "file" => read_write(
                || Ok(b"Content".to_vec()),
                20,
                |content| {
                    assert_eq!(*&content, b"New content");
                    Ok(())
                }),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
            let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;
            let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

            assert_read!(file, "Content");
            assert_seek!(file, 0, Start);
            assert_write!(file, "New content");

            assert_close!(file);

            assert_close!(root);
        });
    }

    #[test]
    fn read_dirents_large_buffer() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only(|| Ok(b"/dev/fs /".to_vec())),
                "passwd" => read_only(|| Ok(b"[redacted]".to_vec())),
                "shells" => read_only(|| Ok(b"/bin/bash".to_vec())),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only(|| Ok(b"# Empty".to_vec())),
                },
            },
            "files" => read_only(|| Ok(b"Content".to_vec())),
            "more" => read_only(|| Ok(b"Content".to_vec())),
            "uname" => read_only(|| Ok(b"Fuchsia".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
            "files" => read_only(|| Ok(b"Content".to_vec())),
            "more" => read_only(|| Ok(b"Content".to_vec())),
            "uname" => read_only(|| Ok(b"Fuchsia".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
        });
    }

    #[test]
    fn read_dirents_very_small_buffer() {
        let root = pseudo_directory! {
            "file" => read_only(|| Ok(b"Content".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
            // Entry header is 10 bytes, so this read should not be able to return a single entry.
            assert_read_dirents_err!(root, 8, Status::BUFFER_TOO_SMALL);
        });
    }

    #[test]
    fn read_dirents_rewind() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! { },
            "files" => read_only(|| Ok(b"Content".to_vec())),
            "more" => read_only(|| Ok(b"Content".to_vec())),
            "uname" => read_only(|| Ok(b"Fuchsia".to_vec())),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, async move |root| {
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
        });
    }

    #[test]
    fn node_reference_ignores_read_access() {
        let root = pseudo_directory! {
            "file" => read_only(|| Ok(b"Content".to_vec())),
        };

        run_server_client(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_READABLE,
            root,
            async move |root| {
                {
                    let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                    let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

                    assert_read!(file, "Content");
                    assert_close!(file);
                }

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
            "file" => read_only(|| Ok(b"Content".to_vec())),
        };

        run_server_client(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_WRITABLE,
            root,
            async move |root| {
                {
                    let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                    let file = open_get_file_proxy_assert_ok!(&root, flags, "file");

                    assert_read!(file, "Content");
                    assert_close!(file);
                }

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
    fn node_reference_allows_read_dirent() {
        let root = pseudo_directory! {
            "etc" => pseudo_directory! {
                "fstab" => read_only(|| Ok(b"/dev/fs /".to_vec())),
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only(|| Ok(b"# Empty".to_vec())),
                },
            },
            "files" => read_only(|| Ok(b"Content".to_vec())),
        };

        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_FLAG_NODE_REFERENCE,
            root,
            async move |root| {
                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected
                        .add(DIRENT_TYPE_DIRECTORY, b".")
                        .add(DIRENT_TYPE_DIRECTORY, b"etc")
                        .add(DIRENT_TYPE_FILE, b"files");

                    assert_read_dirents!(root, 1000, expected.into_vec());
                }

                {
                    let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
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
                    let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                    let ssh_dir = open_get_directory_proxy_assert_ok!(&root, flags, "etc/ssh");

                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"sshd_config");

                    assert_read_dirents!(ssh_dir, 1000, expected.into_vec());
                    assert_close!(ssh_dir);
                }

                assert_close!(root);
            },
        );
    }
}
