// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Connection to a directory that can be modified by the client though a FIDL connection.

use crate::{
    common::send_on_open_with_error,
    directory::{
        common::new_connection_validate_flags,
        connection::{
            handle_requests, BaseConnection, BaseConnectionClient, ConnectionState,
            DerivedConnection, DerivedDirectoryRequest, DirectoryRequestType,
        },
        entry::DirectoryEntry,
        entry_container,
        mutable::entry_constructor::NewEntryType,
    },
    execution_scope::ExecutionScope,
    path::Path,
    registry::TokenRegistryClient,
};

use {
    failure::Error,
    fidl::{endpoints::ServerEnd, Handle},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryObject, DirectoryRequest, NodeInfo, NodeMarker, OPEN_FLAG_CREATE,
        OPEN_FLAG_DESCRIBE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    futures::future::BoxFuture,
    std::sync::Arc,
};

/// This is an API a mutable directory needs to implement, in order for a `MutableConnection` to be
/// able to interact with this it.
pub trait MutableConnectionClient<TraversalPosition>:
    BaseConnectionClient<TraversalPosition> + entry_container::DirectlyMutable
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    fn into_base_connection_client(
        self: Arc<Self>,
    ) -> Arc<dyn BaseConnectionClient<TraversalPosition>>;

    fn into_directly_mutable(self: Arc<Self>) -> Arc<dyn entry_container::DirectlyMutable>;

    fn into_token_registry_client(self: Arc<Self>) -> Arc<dyn TokenRegistryClient>;
}

impl<TraversalPosition, T> MutableConnectionClient<TraversalPosition> for T
where
    TraversalPosition: Default + Send + Sync + 'static,
    T: BaseConnectionClient<TraversalPosition> + entry_container::DirectlyMutable + 'static,
{
    fn into_base_connection_client(
        self: Arc<Self>,
    ) -> Arc<dyn BaseConnectionClient<TraversalPosition>> {
        self as Arc<dyn BaseConnectionClient<TraversalPosition>>
    }

    fn into_directly_mutable(self: Arc<Self>) -> Arc<dyn entry_container::DirectlyMutable> {
        self as Arc<dyn entry_container::DirectlyMutable>
    }

    fn into_token_registry_client(self: Arc<Self>) -> Arc<dyn TokenRegistryClient> {
        self as Arc<dyn TokenRegistryClient>
    }
}

pub struct MutableConnection<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    base: BaseConnection<Self, TraversalPosition>,
}

impl<TraversalPosition> DerivedConnection<TraversalPosition>
    for MutableConnection<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    type Directory = dyn MutableConnectionClient<TraversalPosition>;

    fn new(scope: ExecutionScope, directory: Arc<Self::Directory>, flags: u32) -> Self {
        MutableConnection {
            base: BaseConnection::<Self, TraversalPosition>::new(scope, directory, flags),
        }
    }

    fn create_connection(
        scope: ExecutionScope,
        directory: Arc<Self::Directory>,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
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

        let task = handle_requests::<Self, TraversalPosition>(requests, connection);
        // If we failed to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do, but to ignore the request.  Connection will be closed when the connection
        // object is dropped.
        let _ = scope.spawn(Box::pin(task));
    }

    fn entry_not_found(
        scope: ExecutionScope,
        parent: Arc<dyn DirectoryEntry>,
        flags: u32,
        mode: u32,
        name: &str,
        path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, Status> {
        if flags & OPEN_FLAG_CREATE == 0 {
            return Err(Status::NOT_FOUND);
        }

        let type_ = NewEntryType::from_flags_and_mode(flags, mode, path.is_dir())?;

        let entry_constructor = match scope.entry_constructor() {
            None => return Err(Status::NOT_SUPPORTED),
            Some(constructor) => constructor,
        };

        entry_constructor.create_entry(parent, type_, name, path)
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
}

impl<TraversalPosition> MutableConnection<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    async fn handle_derived_request(
        &mut self,
        request: DerivedDirectoryRequest,
    ) -> Result<ConnectionState, Error> {
        match request {
            DerivedDirectoryRequest::Unlink { path, responder } => {
                self.handle_unlink(path, |status| responder.send(status.into_raw()))?;
            }
            DerivedDirectoryRequest::GetToken { responder } => {
                self.handle_get_token(|status, token| responder.send(status.into_raw(), token))?;
            }
            DerivedDirectoryRequest::Rename { src, dst_parent_token, dst, responder } => {
                self.handle_rename(src, dst_parent_token, dst, |status| {
                    responder.send(status.into_raw())
                })?;
            }
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_unlink<R>(&mut self, path: String, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if self.base.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::ACCESS_DENIED);
        }

        if path == "/" || path == "" || path == "." || path == "./" {
            return responder(Status::BAD_PATH);
        }

        let mut path = match Path::validate_and_split(path) {
            Ok(path) => path,
            Err(status) => return responder(status),
        };

        let entry_name = match path.next() {
            None => return responder(Status::BAD_PATH),
            Some(name) => name.to_string(),
        };

        // We do not support traversal for the `Unlink` operation for now.  It is non-trivial, as
        // we need to go from node to node and we do not store their type information.  One
        // solution is to add `unlink` to `DirectoryEntry`, similar to `open`.  But, unlike `open`
        // it requires the operation to stay on the stack, even when we are hitting a mount point,
        // as we need to return status over the same connection.  C++ verison of "memfs" does not
        // do traversal, so we are not supporting it here either.  At least for now.
        //
        // Sean (smklein@) and Yifei (yifeit@) both agree that it should be removed from the
        // io.fidl spec.
        if !path.is_empty() {
            return responder(Status::BAD_PATH);
        }

        match self.base.directory.clone().remove_entry_impl(entry_name) {
            Ok(None) => responder(Status::NOT_FOUND),
            Ok(Some(_entry)) => responder(Status::OK),
            Err(status) => responder(status),
        }
    }

    fn handle_get_token<R>(&self, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, Option<Handle>) -> Result<(), fidl::Error>,
    {
        if self.base.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::ACCESS_DENIED, None);
        }

        let token_registry = match self.base.scope.token_registry() {
            None => return responder(Status::NOT_SUPPORTED, None),
            Some(registry) => registry,
        };

        let token = {
            let dir_as_reg_client = self.base.directory.clone().into_token_registry_client();
            match token_registry.get_token(dir_as_reg_client) {
                Err(status) => return responder(status, None),
                Ok(token) => token,
            }
        };

        responder(Status::OK, Some(token))
    }

    fn handle_rename<R>(
        &self,
        src: String,
        dst_parent_token: Handle,
        dst: String,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if self.base.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::ACCESS_DENIED);
        }

        let token_registry = match self.base.scope.token_registry() {
            None => return responder(Status::NOT_SUPPORTED),
            Some(registry) => registry,
        };

        let dst_parent = match token_registry.get_container(dst_parent_token) {
            Err(status) => return responder(status),
            Ok(None) => return responder(Status::NOT_FOUND),
            Ok(Some(entry)) => entry,
        };

        match entry_container::rename_helper(
            self.base.directory.clone().into_directly_mutable(),
            src,
            dst_parent.into_directly_mutable(),
            dst,
        ) {
            Ok(()) => responder(Status::OK),
            Err(status) => responder(status),
        }
    }
}
