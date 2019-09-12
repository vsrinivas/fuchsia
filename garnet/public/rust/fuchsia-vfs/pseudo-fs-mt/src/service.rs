// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementations of a service endpoint.

mod common;
mod connection;

#[cfg(test)]
mod tests;

use crate::{
    common::send_on_open_with_error,
    directory::entry::{DirectoryEntry, EntryInfo},
    execution_scope::ExecutionScope,
    path::Path,
    service::{common::new_connection_validate_flags, connection::Connection},
};

use {
    fidl::{
        self,
        endpoints::{RequestStream, ServerEnd},
    },
    fidl_fuchsia_io::{
        NodeMarker, DIRENT_TYPE_SERVICE, INO_UNKNOWN, OPEN_FLAG_NODE_REFERENCE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_async::Channel,
    fuchsia_zircon::Status,
    futures::future::Future,
    std::sync::Arc,
};

/// Constructs a node in your file system that will host a service that implements a statically
/// specified FIDL protocol.  `ServerRequestStream` specifies the type of the server side of this
/// protocol.
///
/// `create_server` is a callback that is invoked when a new connection to the file system node is
/// established.  The connection is reinterpreted as a `ServerRequestStream` FIDL connection and
/// passed to `create_server`.  A task produces by the `create_server` callback is execution in the
/// same [`ExecutionScope`] as the one hosting current connection.
///
/// Prefer to use this method, if the type of your FIDL protocol is statically known and you want
/// to use the connection execution scope to serve the protocol requests.  See [`endpoint`] for a
/// lower level version that gives you more flexibility.
pub fn host<ServerRequestStream, CreateServer, Task>(create_server: CreateServer) -> Arc<Service>
where
    ServerRequestStream: RequestStream,
    CreateServer: Fn(ServerRequestStream) -> Task + Send + Sync + 'static,
    Task: Future<Output = ()> + Send + 'static,
{
    endpoint(move |scope, channel| {
        let requests = RequestStream::from_channel(channel);
        let task = create_server(requests);
        // There is no way to report executor failures, and if it is failing it must be shutting
        // down.
        let _ = scope.spawn(task);
    })
}

/// Constructs a node in your file system that will host a service.
///
/// This is a lower level version of [`host`], which you should prefer if it matches your use case.
/// Unlike [`host`], `endpoint` uses a callback that will just consume the server side of the
/// channel when it is connected to the service node.  It is up to the implementer of the `open`
/// callback to decide how to interpret the channel (allowing for non-static protocol selection)
/// and/or where the processing of the messages received over the channel will occur (but the
/// [`ExecutionScope`] connected to the connection is provided every time).
pub fn endpoint<Open>(open: Open) -> Arc<Service>
where
    Open: Fn(ExecutionScope, Channel) + Send + Sync + 'static,
{
    Arc::new(Service { open: Box::new(open) })
}

/// Represents a node in the file system that hosts a service.  Opening a connection to this node
/// will switch to FIDL protocol that is different from the file system protocols, described in
/// io.fidl.  See there for additional details.
///
/// Use [`host`] or [`endpoint`] to construct nodes of this type.
pub struct Service {
    open: Box<dyn Fn(ExecutionScope, Channel) + Send + Sync>,
}

impl Service {
    fn open_as_node(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        Connection::create_connection(scope, flags, mode, server_end);
    }

    fn open_as_service(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        // There will be a few cases in this methods and one in `<Service as
        // DirectoryEntry>::open()` when we encounter an error while trying to process requests.
        //
        // We are not supposed to send `OnOpen` events if the node is opened without
        // `OPEN_FLAG_NODE_REFERENCE` as the `server_end` might be interpreted as a protocol that
        // does not support Node.  So the only thing we can do is to close the channel.  We could
        // have send an epitaph, but they are not supported by Rust and in the next version of
        // io.fidl the error handing will be different anyways.

        let flags = match new_connection_validate_flags(flags, mode) {
            Ok(updated) => updated,
            Err(_status) => {
                // See comment at the beginning of the method.
                return;
            }
        };

        debug_assert!(flags == OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);

        match Channel::from_channel(server_end.into_channel()) {
            Ok(channel) => (self.open)(scope, channel),
            Err(_err) => {
                // See comment at the beginning of the method.
                return;
            }
        }
    }
}

impl DirectoryEntry for Service {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if !path.is_empty() {
            // See comment at the beginning of [`Service::open_as_service`].
            if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
                send_on_open_with_error(flags, server_end, Status::NOT_DIR);
            }
            return;
        }

        if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
            self.open_as_node(scope, flags, mode, server_end);
        } else {
            self.open_as_service(scope, flags, mode, server_end);
        }
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_SERVICE)
    }

    fn can_hardlink(&self) -> bool {
        true
    }
}
