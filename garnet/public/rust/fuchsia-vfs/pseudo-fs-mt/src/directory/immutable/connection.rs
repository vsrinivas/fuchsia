// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Connection to a directory that can not be modified by the client, no matter what permissions
//! the client has on the FIDL connection.

use crate::{
    directory::connection::{
        BaseConnection, BaseConnectionClient, ConnectionState, DerivedConnection,
        DerivedDirectoryRequest, DirectoryRequestType,
    },
    execution_scope::ExecutionScope,
};

use {
    failure::Error, fidl_fuchsia_io::DirectoryRequest, fuchsia_zircon::sys::ZX_ERR_NOT_SUPPORTED,
    futures::future::BoxFuture, std::sync::Arc,
};

pub trait ImmutableConnectionClient<TraversalPosition>:
    BaseConnectionClient<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
}

impl<TraversalPosition, T> ImmutableConnectionClient<TraversalPosition> for T
where
    TraversalPosition: Default + Send + Sync + 'static,
    T: BaseConnectionClient<TraversalPosition> + 'static,
{
}

pub struct ImmutableConnection<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    base: BaseConnection<Self, TraversalPosition>,
}

impl<TraversalPosition> DerivedConnection<TraversalPosition>
    for ImmutableConnection<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    type Directory = dyn ImmutableConnectionClient<TraversalPosition>;

    fn new(scope: ExecutionScope, directory: Arc<Self::Directory>, flags: u32) -> Self {
        ImmutableConnection {
            base: BaseConnection::<Self, TraversalPosition>::new(scope, directory, flags),
        }
    }

    fn handle_request(
        &mut self,
        request: DirectoryRequest,
    ) -> BoxFuture<Result<ConnectionState, Error>> {
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

impl<TraversalPosition> ImmutableConnection<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
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
        }
        Ok(ConnectionState::Alive)
    }
}
