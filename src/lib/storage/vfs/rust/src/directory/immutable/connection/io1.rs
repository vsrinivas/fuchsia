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
            io1::{
                handle_requests, BaseConnection, BaseConnectionClient, ConnectionState,
                DerivedConnection, DerivedDirectoryRequest, DirectoryRequestType,
            },
            util::OpenDirectory,
        },
        entry::DirectoryEntry,
    },
    execution_scope::ExecutionScope,
    path::Path,
};

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryObject, DirectoryRequest, NodeInfo, NodeMarker, OPEN_FLAG_CREATE,
        OPEN_FLAG_DESCRIBE,
    },
    fuchsia_zircon::{sys::ZX_ERR_NOT_SUPPORTED, Status},
    futures::future::BoxFuture,
    std::sync::Arc,
};

pub trait ImmutableConnectionClient: BaseConnectionClient {}

impl<T> ImmutableConnectionClient for T where T: BaseConnectionClient + 'static {}

pub struct ImmutableConnection {
    base: BaseConnection<Self>,
}

impl DerivedConnection for ImmutableConnection {
    type Directory = dyn ImmutableConnectionClient;

    fn new(scope: ExecutionScope, directory: OpenDirectory<Self::Directory>, flags: u32) -> Self {
        ImmutableConnection { base: BaseConnection::<Self>::new(scope, directory, flags) }
    }

    fn create_connection(
        scope: ExecutionScope,
        directory: Arc<Self::Directory>,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let directory = OpenDirectory::new(directory);
        let flags = match new_connection_validate_flags(flags, mode) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        let (requests, control_handle) =
            match ServerEnd::<DirectoryMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()
            {
                Ok((requests, control_handle)) => (requests, control_handle),
                Err(_) => {
                    // As we report all errors on `server_end`, if we failed to send an error over
                    // this connection, there is nowhere to send the error tothe error to.
                    return;
                }
            };

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            let mut info = NodeInfo::Directory(DirectoryObject);
            match control_handle.send_on_open_(Status::OK.into_raw(), Some(&mut info)) {
                Ok(()) => (),
                Err(_) => return,
            }
        }

        let connection = Self::new(scope.clone(), directory, flags);

        let task = handle_requests::<Self>(requests, connection);
        // If we fail to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do - the connection will be closed automatically when the connection object is
        // dropped.
        let _ = scope.spawn(Box::pin(task));
    }

    fn entry_not_found(
        _scope: ExecutionScope,
        _parent: Arc<dyn DirectoryEntry>,
        flags: u32,
        _mode: u32,
        _name: &str,
        _path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, Status> {
        if flags & OPEN_FLAG_CREATE == 0 {
            Err(Status::NOT_FOUND)
        } else {
            Err(Status::NOT_SUPPORTED)
        }
    }

    fn handle_request(
        &mut self,
        request: DirectoryRequest,
    ) -> BoxFuture<'_, Result<ConnectionState, Error>> {
        Box::pin(async move {
            match request.into() {
                DirectoryRequestType::Base(request) => self.base.handle_request(request).await,
                DirectoryRequestType::Derived(request) => {
                    self.handle_derived_request(request).await
                }
            }
        })
    }

    fn get_directory(&self) -> &Self::Directory {
        self.base.directory.as_ref()
    }
}

impl ImmutableConnection {
    async fn handle_derived_request(
        &mut self,
        request: DerivedDirectoryRequest,
    ) -> Result<ConnectionState, Error> {
        match request {
            DerivedDirectoryRequest::Unlink { path: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DerivedDirectoryRequest::GetToken { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED, None)?;
            }
            DerivedDirectoryRequest::Rename { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DerivedDirectoryRequest::SetAttr { responder, .. } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DerivedDirectoryRequest::Sync { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
        }
        Ok(ConnectionState::Alive)
    }
}
