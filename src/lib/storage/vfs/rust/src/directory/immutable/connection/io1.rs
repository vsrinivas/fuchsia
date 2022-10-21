// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Connection to a directory that can not be modified by the client, no matter what permissions
//! the client has on the FIDL connection.

use crate::{
    common::send_on_open_with_error,
    directory::{
        common::new_connection_validate_flags,
        connection::{
            io1::{BaseConnection, ConnectionState, DerivedConnection, WithShutdown},
            util::OpenDirectory,
        },
        entry::DirectoryEntry,
        entry_container,
    },
    execution_scope::ExecutionScope,
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_zircon::Status,
    futures::{channel::oneshot, TryStreamExt},
    std::sync::Arc,
};

pub struct ImmutableConnection {
    base: BaseConnection<Self>,
}

impl ImmutableConnection {
    async fn handle_requests(
        mut self,
        requests: fio::DirectoryRequestStream,
        shutdown: oneshot::Receiver<()>,
    ) {
        let mut requests = requests.with_shutdown(shutdown);
        while let Ok(Some(request)) = requests.try_next().await {
            if !matches!(self.base.handle_request(request).await, Ok(ConnectionState::Alive)) {
                break;
            }
        }
    }
}

impl DerivedConnection for ImmutableConnection {
    type Directory = dyn entry_container::Directory;
    const MUTABLE: bool = false;

    fn new(
        scope: ExecutionScope,
        directory: OpenDirectory<Self::Directory>,
        flags: fio::OpenFlags,
    ) -> Self {
        ImmutableConnection { base: BaseConnection::<Self>::new(scope, directory, flags) }
    }

    fn create_connection(
        scope: ExecutionScope,
        directory: Arc<Self::Directory>,
        flags: fio::OpenFlags,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        // Ensure we close the directory if we fail to create the connection.
        let directory = OpenDirectory::new(directory);

        // TODO(fxbug.dev/82054): These flags should be validated before create_connection is called
        // since at this point the directory resource has already been opened/created.
        let flags = match new_connection_validate_flags(flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        let (requests, control_handle) =
            match ServerEnd::<fio::DirectoryMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()
            {
                Ok((requests, control_handle)) => (requests, control_handle),
                Err(_) => {
                    // As we report all errors on `server_end`, if we failed to send an error over
                    // this connection, there is nowhere to send the error to.
                    return;
                }
            };

        let connection = Self::new(scope.clone(), directory, flags);

        if flags.intersects(fio::OpenFlags::DESCRIBE) {
            match control_handle
                .send_on_open_(Status::OK.into_raw(), Some(&mut connection.base.node_info()))
            {
                Ok(()) => (),
                Err(_) => return,
            }
        }

        // If we fail to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do - the connection will be closed automatically when the connection object is
        // dropped.
        let _ =
            scope.spawn_with_shutdown(|shutdown| connection.handle_requests(requests, shutdown));
    }

    fn entry_not_found(
        _scope: ExecutionScope,
        _parent: Arc<dyn DirectoryEntry>,
        flags: fio::OpenFlags,
        _mode: u32,
        _name: &str,
        _path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, Status> {
        if !flags.intersects(fio::OpenFlags::CREATE) {
            Err(Status::NOT_FOUND)
        } else {
            Err(Status::NOT_SUPPORTED)
        }
    }
}
