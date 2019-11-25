// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A pseudo directory that can be controlled via an mpsc channel.

use {
    crate::directory::{
        controllable::Controllable,
        entry::{DirectoryEntry, EntryInfo},
    },
    failure::Fail,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fuchsia_zircon::Status,
    futures::{
        channel::{
            mpsc::{self, SendError},
            oneshot,
        },
        future::{FusedFuture, FutureExt},
        sink::SinkExt,
        stream::{FusedStream, StreamExt},
    },
    std::{fmt, future::Future, marker::Unpin, pin::Pin, task::{Context, Poll}},
    void::{unreachable, Void},
};

/// Type of errors returned by the [`Controller::open`] future.
#[derive(Debug, Fail)]
pub enum OpenError {
    /// Controlled directory has been destroyed.
    #[fail(display = "Controlled directory has been destroyed.")]
    Terminated,
}

/// Type of errors returned by the [`Controller::add_entry`] future.
#[derive(Debug, Fail)]
pub enum AddEntryError {
    /// Controlled directory has been destroyed.
    #[fail(display = "Controlled directory has been destroyed.")]
    Terminated,
}

/// Type of errors returned by the [`Controller::add_entry_res`] future.
// TODO #[derive(Fail)] does not work here, as it requres `Sync` and `DirectoryEntry` is not
// necessarily `Sync`.  I can think of two solutions: parametrize the whole libary over `Sync`,
// allowing `Fail` in case `DirectoryEntry` is also `Sync` and disalllowing otherwise.  Or
// providing a conversion for this error that will drop the contained directory entry object.
//
// As there are no users for it, I will probably keep it unimplemented for now.
pub enum AddEntryResError<'entries> {
    /// Controlled directory has been destroyed.
    Terminated,
    /// [`Controlled::add_boxed_entry`] has returned an error.
    AddFailed((Status, Box<dyn DirectoryEntry + 'entries>)),
}

impl<'entries> fmt::Debug for AddEntryResError<'entries> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AddEntryResError::Terminated => write!(f, "Terminated"),
            AddEntryResError::AddFailed((status, _)) => {
                f.debug_tuple("AddFailed").field(&status).field(&format_args!("_")).finish()
            }
        }
    }
}

/// Type of errors returned by the [`Controller::remove_entry`] future.
#[derive(Debug, Fail)]
pub enum RemoveEntryError {
    /// Controlled directory has been destroyed.
    #[fail(display = "Controlled directory has been destroyed.")]
    Terminated,
}

/// Type of errors returned by the [`Controller::remove_entry_res`] future.
#[derive(Debug, Fail)]
pub enum RemoveEntryResError {
    /// Controlled directory has been destroyed.
    #[fail(display = "Controlled directory has been destroyed.")]
    Terminated,
    /// [`Controlled::remove_entry`] has returned an error.
    #[fail(display = "`Controlled::remove_entry` has returned an error")]
    RemoveFailed(Status),
}

type AddEntryResponse<'entries> = Result<(), (Status, Box<dyn DirectoryEntry + 'entries>)>;
type RemoveEntryResponse<'entries> = Result<Option<Box<dyn DirectoryEntry + 'entries>>, Status>;

enum Command<'entries> {
    Open {
        flags: u32,
        mode: u32,
        path: Vec<String>,
        server_end: ServerEnd<NodeMarker>,
    },
    AddEntry {
        name: String,
        entry: Box<dyn DirectoryEntry + 'entries>,
    },
    AddEntryAndRespond {
        name: String,
        entry: Box<dyn DirectoryEntry + 'entries>,
        res_sender: oneshot::Sender<AddEntryResponse<'entries>>,
    },
    RemoveEntry {
        name: String,
    },
    RemoveEntryAndRespond {
        name: String,
        res_sender: oneshot::Sender<RemoveEntryResponse<'entries>>,
    },
}

/// This is a "remote control" for a [`DirectoryEntry`] that it also [`Controllable`] wrapped in a
/// [`Controlled`].  An instance of this type is used to "remotely control" a directory entry,
/// allowing addition and removal of entries while not owning the directory directly.  A directory
/// entry is wrapped using a [`controlled()`] method.
pub struct Controller<'entries> {
    controlled: mpsc::Sender<Command<'entries>>,
}

fn check_send_err_is_disconnection(context: &str, err: SendError) {
    if err.is_full() {
        eprintln!(
            "{}: command queue is full.  This should never happen as all the controllers \
             are expected to wait for the command to be delivered and the queue has one slot per \
             controller.",
            context
        );
    } else if !err.is_disconnected() {
        eprintln!(
            "{}: send() returned an error that is both !is_full() and !is_disconnected().",
            context
        );
    }
}

impl<'entries> Controller<'entries> {
    /// Adds a connection to the directory controlled by this controller when `path` is empty, or
    /// to a child entry specified by the `path`.
    ///
    /// In case of any error the `server_end` is dropped and the underlying channel is closed.  As
    /// [`DirectoryEntry::open`] has no retrun value, there is no matching [`open_res`].
    // I wish we could do
    //
    //   pub fn open<Path, PathElem>(
    //       &self,
    //       flags: u32,
    //       mode: u32,
    //       path: Path,
    //       server_end: ServerEnd<NodeMarker>,
    //   ) -> impl Future<Output = Result<(), OpenError>> + 'entries
    //   where
    //       Path: Into<Vec<PathElem>>,
    //       PathElem: Into<String>,
    //
    // as that would allow people to call open() with different arugments, but, unfortunately it
    // does not really remove the allocation.  Only when open() is inlined and is passed a
    // Vec<&str> the allocation seems to be optimized away.  But for the case when Vec<String> is
    // used, the vector is reallocated.
    pub fn open(
        &self,
        flags: u32,
        mode: u32,
        path: Vec<String>,
        server_end: ServerEnd<NodeMarker>,
    ) -> impl Future<Output = Result<(), OpenError>> + 'entries {
        // Cloning the sender allows us to generate a future that does not have any lifetime
        // dependencies on self.
        let mut controlled = self.controlled.clone();
        async move {
            controlled.send(Command::Open { flags, mode, path, server_end }).await.map_err(
                |send_err| {
                    check_send_err_is_disconnection("Controller::open", send_err);
                    OpenError::Terminated
                },
            )
        }
    }

    /// Adds a child entry to the directory controlled by this controller.  The directory will own
    /// the child entry item and will run it as part of the directory own `poll()` invocation.
    ///
    /// In case of any error new entry is silently dropped.  But see [`add_entry_res`] in case you
    /// want to process errors.
    pub fn add_entry<Name, Entry>(
        &self,
        name: Name,
        entry: Entry,
    ) -> impl Future<Output = Result<(), AddEntryError>> + 'entries
    where
        Name: Into<String>,
        Entry: DirectoryEntry + 'entries,
    {
        // Even though add_boxed_entry() will do `name.into()` on it's own, we still need to do it
        // here.  Otherwise the compiler will require explicit `'entries` lifetime on the `Name`
        // constraint.
        let name = name.into();
        self.add_boxed_entry(name, Box::new(entry))
    }

    /// Adds a child entry to the directory controlled by this controller.  The directory will own
    /// the child entry item and will run it as part of the directory own `poll()` invocation.
    ///
    /// In case of any error new entry is silently dropped.  But see [`add_boxed_entry_res`] in case you
    /// want to process errors.
    pub fn add_boxed_entry<Name>(
        &self,
        name: Name,
        entry: Box<dyn DirectoryEntry + 'entries>,
    ) -> impl Future<Output = Result<(), AddEntryError>> + 'entries
    where
        Name: Into<String>,
    {
        // Cloning the sender allows us to generate a future that does not have any lifetime
        // dependencies on self.
        let mut controlled = self.controlled.clone();
        let name = name.into();
        async move {
            controlled.send(Command::AddEntry { name, entry }).await.map_err(|send_err| {
                check_send_err_is_disconnection("Controller::add_boxed_entry", send_err);
                AddEntryError::Terminated
            })
        }
    }

    /// Adds a child entry to the directory controlled by this controller.  The directory will own
    /// the child entry item and will run it as part of the directory own `poll()` invocation.
    ///
    /// In case of any error new entry is returned along with the status code.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    pub fn add_entry_res<Name, Entry>(
        &self,
        name: Name,
        entry: Entry,
    ) -> impl Future<Output = Result<(), AddEntryResError<'entries>>> + 'entries
    where
        Name: Into<String>,
        Entry: DirectoryEntry + 'entries,
    {
        // See add_entry() for the reasoning on why this copy `name.into()` is required.
        let name = name.into();
        self.add_boxed_entry_res(name, Box::new(entry))
    }

    /// Adds a child entry to the directory controlled by this controller.  The directory will own
    /// the child entry item and will run it as part of the directory own `poll()` invocation.
    ///
    /// In case of any error new entry is returned along with the status code.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    pub fn add_boxed_entry_res<Name>(
        &self,
        name: Name,
        entry: Box<dyn DirectoryEntry + 'entries>,
    ) -> impl Future<Output = Result<(), AddEntryResError<'entries>>> + 'entries
    where
        Name: Into<String>,
    {
        // Cloning the sender allows us to generate a future that does not have any lifetime
        // dependencies on self.
        let mut controlled = self.controlled.clone();
        let name = name.into();
        let (res_sender, res_receiver) = oneshot::channel();
        async move {
            controlled
                .send(Command::AddEntryAndRespond { name, entry, res_sender })
                .await
                .map_err(|send_err| {
                    check_send_err_is_disconnection("Controller::add_boxed_entry_res", send_err);
                    AddEntryResError::Terminated
                })?;

            match res_receiver.await {
                Ok(res) => res.map_err(AddEntryResError::AddFailed),
                Err(oneshot::Canceled) => Err(AddEntryResError::Terminated),
            }
        }
    }

    /// Removes a child entry from this directory.  Existing entry is dropped.  But see
    /// [`remove_entry_res`] for an alternative.  If the entry was not found or in case of an error
    /// the call is just ignored.
    pub fn remove_entry<Name>(
        &self,
        name: Name,
    ) -> impl Future<Output = Result<(), RemoveEntryError>> + 'entries
    where
        Name: Into<String>,
    {
        // Cloning the sender allows us to generate a future that does not have any lifetime
        // dependencies on self.
        let mut controlled = self.controlled.clone();
        let name = name.into();
        async move {
            controlled.send(Command::RemoveEntry { name }).await.map_err(|send_err| {
                check_send_err_is_disconnection("Controller::remove_entry", send_err);
                RemoveEntryError::Terminated
            })
        }
    }

    /// Removes a child entry from this directory and returns it, if it was found.  Nothing happens
    /// in case of an error.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    pub fn remove_entry_res<Name>(
        &self,
        name: Name,
    ) -> impl Future<
        Output = Result<Option<Box<dyn DirectoryEntry + 'entries>>, RemoveEntryResError>,
    > + 'entries
    where
        Name: Into<String>,
    {
        // Cloning the sender allows us to generate a future that does not have any lifetime
        // dependencies on self.
        let mut controlled = self.controlled.clone();
        let name = name.into();
        let (res_sender, res_receiver) = oneshot::channel();
        async move {
            controlled.send(Command::RemoveEntryAndRespond { name, res_sender }).await.map_err(
                |send_err| {
                    check_send_err_is_disconnection("Controller::remove_entry_res", send_err);
                    RemoveEntryResError::Terminated
                },
            )?;

            match res_receiver.await {
                Ok(res) => res.map_err(RemoveEntryResError::RemoveFailed),
                Err(oneshot::Canceled) => Err(RemoveEntryResError::Terminated),
            }
        }
    }
}

/// This is a wrapper around a [`DirectoryEntry`] that it also [`Controllable`].  A [`Controller`]
/// instance is used to "remotely control" this directory entry, allowing addition and removal of
/// entries while not owning the entry.  Corresponding controller is returned from the
/// [`controlled()`] method.
pub struct Controlled<'entries> {
    /// Connection to the controller.
    controller: mpsc::Receiver<Command<'entries>>,

    /// Wrapped entry.
    controllable: Box<dyn Controllable<'entries> + 'entries>,
}

/// Given a directory that can be controlled, create a "controller" for it.  Controller allows
/// directory content to be updated while the directory is part of a larger tree.
pub fn controlled<'entries, Entry: 'entries>(
    controllable: Entry,
) -> (Controller<'entries>, Controlled<'entries>)
where
    Entry: Controllable<'entries>,
{
    let (sender, receiver) = mpsc::channel(1);
    (
        Controller { controlled: sender },
        Controlled { controller: receiver, controllable: Box::new(controllable) },
    )
}

impl<'entries> Controlled<'entries> {
    /// Adds a child entry to the directory.  The directory will own the child entry item and will
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
        self.controllable.add_boxed_entry(name, Box::new(entry))
    }

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, the entry will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    pub fn remove_entry(
        &mut self,
        name: &str,
    ) -> Result<Option<Box<dyn DirectoryEntry + 'entries>>, Status> {
        self.controllable.remove_entry(name)
    }

    fn handle_command(&mut self, command: Command<'entries>) {
        match command {
            Command::Open { flags, mode, path, server_end } => {
                // As the controller did not ask for the result, we can only ignore any potential
                // errors here.
                let _ = self.controllable.open(
                    flags,
                    mode,
                    &mut path.iter().map(|s| s.as_str()),
                    server_end,
                );
            }
            Command::AddEntry { name, entry } => {
                // As the controller did not ask for the result, we can only ignore any potential
                // errors here.
                let _ = self.controllable.add_boxed_entry(&name, entry);
            }
            Command::AddEntryAndRespond { name, entry, res_sender } => {
                let res = self.controllable.add_boxed_entry(&name, entry);
                // Failure to send a response should indicate that the controller has been
                // destroyed.
                let _ = res_sender.send(res);
            }
            Command::RemoveEntry { name } => {
                // As the controller did not ask for the result, we can only ignore any potential
                // errors here.
                let _ = self.remove_entry(&name);
            }
            Command::RemoveEntryAndRespond { name, res_sender } => {
                let res = self.remove_entry(&name);
                // Failure to send a response should indicate that the controller has been
                // destroyed.
                let _ = res_sender.send(res);
            }
        }
    }
}

impl<'entries> DirectoryEntry for Controlled<'entries> {
    fn open(
        &mut self,
        flags: u32,
        mode: u32,
        path: &mut dyn Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) {
        self.controllable.open(flags, mode, path, server_end);
    }

    fn entry_info(&self) -> EntryInfo {
        self.controllable.entry_info()
    }
}

impl<'entries> Unpin for Controlled<'entries> {}

impl<'entries> Future for Controlled<'entries> {
    type Output = Void;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        // NOTE See `Simple::poll` for discussion on why we need a loop here.
        //
        // It should be safe to exhaust the `controller` queue first and then run the child tree.
        // `cx` should be set to trigger in case a new command is added to the queue.  So if
        // anything in the child tree causes a new command to be queued, `cx` will be primed and
        // the executor will run this method again.

        while !self.controller.is_terminated() {
            match self.controller.poll_next_unpin(cx) {
                Poll::Pending => break,
                Poll::Ready(None) => break,
                Poll::Ready(Some(command)) => self.handle_command(command),
            }
        }

        match self.controllable.poll_unpin(cx) {
            Poll::Pending => (),
            Poll::Ready(x) => unreachable(x),
        };

        Poll::Pending
    }
}

impl<'entries> FusedFuture for Controlled<'entries> {
    fn is_terminated(&self) -> bool {
        self.controllable.is_terminated() && self.controller.is_terminated()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use {
        crate::directory::{simple, test_utils::run_server_client},
        crate::file::simple::read_only_static,
        fidl::endpoints::{create_proxy, ServerEnd},
        fidl_fuchsia_io::{
            DirectoryMarker, DirectoryObject, FileMarker, NodeInfo, NodeMarker, DIRENT_TYPE_FILE,
            INO_UNKNOWN, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE, WATCH_MASK_ADDED,
            WATCH_MASK_EXISTING, WATCH_MASK_IDLE, WATCH_MASK_REMOVED,
        },
        proc_macro_hack::proc_macro_hack,
    };

    // Create level import of this macro does not affect nested modules.  And as attributes can
    // only be applied to the whole "use" directive, this need to be present here and need to be
    // separate form the above.  "use crate::pseudo_directory" generates a warning refering to
    // "issue #52234 <https://github.com/rust-lang/rust/issues/52234>".
    #[proc_macro_hack(support_nested)]
    use fuchsia_vfs_pseudo_fs_macros::pseudo_directory;

    /// Creates a pseudo directory tree and wraps it with a controller.  Assigns the controller to
    /// the specified variable and returns the root of the tree.  This allows for further
    /// composition into larger trees.
    // $controller needs to be an uninitialized variable.  There seems to be no way to do this with a
    // function.  See
    //
    //     https://internals.rust-lang.org/t/pre-rfc-allow-passing-uninitialized-values-to-functions
    //
    macro_rules! controlled_pseudo_directory {
        ($controller:ident -> { $($definition:tt)* }) => {{
            let res = controlled(pseudo_directory! { $( $definition )* } );
            $controller = res.0;
            res.1
        }};

        ($controller:ident -> $($definition:tt)*) => {{
            let res = controlled(pseudo_directory! { $( $definition )* } );
            $controller = res.0;
            res.1
        }};
    }

    #[test]
    fn empty_directory() {
        let (_, root) = controlled(simple::empty());
        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                assert_close!(root);
            }
        });
    }

    #[test]
    fn simple_open() {
        let (controller, root) = controlled(simple::empty());
        run_server_client(OPEN_RIGHT_READABLE, root, |_root| {
            async move {
                let (proxy, server_end) = create_proxy::<DirectoryMarker>()
                    .expect("Failed to create connection endpoints");

                controller
                    .open(OPEN_RIGHT_READABLE, 0, vec![], ServerEnd::new(server_end.into_channel()))
                    .await
                    .unwrap();
                assert_describe!(proxy, NodeInfo::Directory(DirectoryObject));
                assert_close!(proxy);
            }
        });
    }

    #[test]
    fn simple_add_file() {
        let (controller, root) = controlled(simple::empty());
        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                {
                    let file = read_only_static("Content");
                    controller.add_entry("file", file).await.unwrap();
                }

                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                open_as_file_assert_content!(&root, flags, "file", "Content");
                assert_close!(root);
            }
        });
    }

    #[test]
    fn add_file_to_empty() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory!(controller -> {}),
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

                open_as_file_assert_err!(&root, flags, "etc/fstab", Status::NOT_FOUND);

                {
                    let fstab = read_only_static("/dev/fs /");
                    controller.add_entry("fstab", fstab).await.unwrap();
                }

                open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
                assert_close!(root);
            }
        });
    }

    #[test]
    fn in_tree_open() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory! {
                controller ->
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let (proxy, server_end) = create_proxy::<DirectoryMarker>()
                    .expect("Failed to create connection endpoints");

                controller
                    .open(
                        OPEN_RIGHT_READABLE,
                        0,
                        vec![],
                        ServerEnd::<NodeMarker>::new(server_end.into_channel()),
                    )
                    .await
                    .unwrap();

                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                open_as_file_assert_content!(&proxy, flags, "ssh/sshd_config", "# Empty");
                assert_close!(proxy);

                assert_close!(root);
            }
        });
    }

    #[test]
    fn in_tree_open_path_one_component() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory! {
                controller ->
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let (proxy, server_end) = create_proxy::<DirectoryMarker>()
                    .expect("Failed to create connection endpoints");

                controller
                    .open(
                        OPEN_RIGHT_READABLE,
                        0,
                        vec_string!["ssh"],
                        ServerEnd::<NodeMarker>::new(server_end.into_channel()),
                    )
                    .await
                    .unwrap();

                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                open_as_file_assert_content!(&proxy, flags, "sshd_config", "# Empty");
                assert_close!(proxy);

                assert_close!(root);
            }
        });
    }

    #[test]
    fn in_tree_open_path_two_components() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory! {
                controller ->
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let (proxy, server_end) =
                    create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

                controller
                    .open(
                        OPEN_RIGHT_READABLE,
                        0,
                        vec_string!["ssh", "sshd_config"],
                        ServerEnd::<NodeMarker>::new(server_end.into_channel()),
                    )
                    .await
                    .unwrap();

                assert_read!(&proxy, "# Empty");
                assert_close!(proxy);

                assert_close!(root);
            }
        });
    }

    #[test]
    fn in_tree_add_file() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory! {
                controller ->
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
                "passwd" => read_only_static("[redacted]"),
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

                open_as_file_assert_err!(&root, flags, "etc/fstab", Status::NOT_FOUND);
                open_as_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

                {
                    let fstab = read_only_static("/dev/fs /");
                    controller.add_entry("fstab", fstab).await.unwrap();
                }

                open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
                open_as_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

                assert_close!(root);
            }
        });
    }

    #[test]
    fn in_tree_remove_file() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory! {
                controller ->
                "fstab" => read_only_static("/dev/fs /"),
                "passwd" => read_only_static("[redacted]"),
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

                open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
                open_as_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

                let o_passwd = controller.remove_entry_res("passwd").await.unwrap();
                match o_passwd {
                    None => panic!("remove_entry_res() did not find 'passwd'"),
                    Some(passwd) => {
                        let entry_info = passwd.entry_info();
                        assert_eq!(entry_info, EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE));
                    }
                }

                open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
                open_as_file_assert_err!(&root, flags, "etc/passwd", Status::NOT_FOUND);

                assert_close!(root);
            }
        });
    }

    #[test]
    fn in_tree_move_file() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory! {
                controller ->
                "fstab" => read_only_static("/dev/fs /"),
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

                open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
                open_as_file_assert_err!(&root, flags, "etc/passwd", Status::NOT_FOUND);

                let fstab = controller
                    .remove_entry_res("fstab")
                    .await
                    .unwrap()
                    .expect("remove_entry_res() did not find 'fstab'");

                controller.add_boxed_entry("passwd", fstab).await.unwrap();

                open_as_file_assert_err!(&root, flags, "etc/fstab", Status::NOT_FOUND);
                open_as_file_assert_content!(&root, flags, "etc/passwd", "/dev/fs /");

                assert_close!(root);
            }
        });
    }

    #[test]
    fn watch_addition() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory! {
                controller ->
                "ssh" => pseudo_directory! {
                    "sshd_config" => read_only_static("# Empty"),
                },
                "passwd" => read_only_static("[redacted]"),
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

                open_as_file_assert_err!(&root, flags, "etc/fstab", Status::NOT_FOUND);
                open_as_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

                let etc = open_get_directory_proxy_assert_ok!(&root, flags, "etc");

                let watch_mask =
                    WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
                let watcher = assert_watch!(etc, watch_mask);

                assert_watcher_one_message_watched_events!(
                    watcher,
                    { EXISTING, "." },
                    { EXISTING, "passwd" },
                    { EXISTING, "ssh" },
                );
                assert_watcher_one_message_watched_events!(watcher, { IDLE, vec![] });

                {
                    let fstab = read_only_static("/dev/fs /");
                    controller.add_entry("fstab", fstab).await.unwrap();
                }

                assert_watcher_one_message_watched_events!(watcher, { ADDED, "fstab" });

                open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
                open_as_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

                assert_close!(root);
            }
        });
    }

    #[test]
    fn watch_removal() {
        let controller;
        let root = pseudo_directory! {
            "etc" => controlled_pseudo_directory! {
                controller ->
                "fstab" => read_only_static("/dev/fs /"),
                "passwd" => read_only_static("[redacted]"),
            },
        };

        run_server_client(OPEN_RIGHT_READABLE, root, |root| {
            async move {
                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;

                open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
                open_as_file_assert_content!(&root, flags, "etc/passwd", "[redacted]");

                let etc = open_get_directory_proxy_assert_ok!(&root, flags, "etc");

                let watch_mask =
                    WATCH_MASK_EXISTING | WATCH_MASK_IDLE | WATCH_MASK_ADDED | WATCH_MASK_REMOVED;
                let watcher = assert_watch!(etc, watch_mask);

                assert_watcher_one_message_watched_events!(
                    watcher,
                    { EXISTING, "." },
                    { EXISTING, "fstab" },
                    { EXISTING, "passwd" },
                );
                assert_watcher_one_message_watched_events!(watcher, { IDLE, vec![] });

                let o_passwd = controller.remove_entry_res("passwd").await.unwrap();
                match o_passwd {
                    None => panic!("remove_entry_res() did not find 'passwd'"),
                    Some(passwd) => {
                        let entry_info = passwd.entry_info();
                        assert_eq!(entry_info, EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE));
                    }
                }

                assert_watcher_one_message_watched_events!(watcher, { REMOVED, "passwd" });

                open_as_file_assert_content!(&root, flags, "etc/fstab", "/dev/fs /");
                open_as_file_assert_err!(&root, flags, "etc/passwd", Status::NOT_FOUND);

                assert_close!(root);
            }
        });
    }
}
