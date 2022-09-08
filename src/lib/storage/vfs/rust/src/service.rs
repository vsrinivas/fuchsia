// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementations of a service endpoint.

pub(crate) mod common;
pub(crate) mod connection;

#[cfg(test)]
mod tests;

use crate::{
    common::send_on_open_with_error,
    directory::entry::{DirectoryEntry, EntryInfo},
    execution_scope::ExecutionScope,
    path::Path,
    service::{common::new_connection_validate_flags, connection::io1::Connection},
};

use {
    anyhow::Error,
    fidl::{
        self,
        endpoints::{RequestStream, ServerEnd},
        epitaph::ChannelEpitaphExt as _,
    },
    fidl_fuchsia_io as fio,
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
/// fuchsia.io.  See there for additional details.
///
/// Use [`host`] or [`endpoint`] to construct nodes of this type.
pub struct Service {
    open: Box<dyn Fn(ExecutionScope, Channel) + Send + Sync>,
}

impl Service {
    fn open_as_node(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        Connection::create_connection(scope, flags, mode, server_end);
    }

    fn open_as_service(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        let channel = match Channel::from_channel(server_end.into_channel()) {
            Ok(channel) => channel,
            Err(_) => {
                // We were unable to convert the channel into an async channel, which most likely
                // means the channel was invalid in some way, so there's nothing we can do.
                return;
            }
        };

        // If this returns an error, it will be because it failed to send the OnOpen event, in which
        // case there's nothing we can do.
        let describe = |channel, status: Result<(), Status>| -> Result<Channel, Error> {
            if !flags.contains(fio::OpenFlags::DESCRIBE) {
                return Ok(channel);
            }
            // TODO(https://fxbug.dev/104708): This is a bit crude but there seems to be no other
            // way of sending on_open_ using FIDL and then getting the channel back.
            let request_stream = fio::NodeRequestStream::from_channel(channel);
            let (status, mut node_info) = match status {
                Ok(()) => (Status::OK, Some(fio::NodeInfoDeprecated::Service(fio::Service))),
                Err(status) => (status, None),
            };
            request_stream.control_handle().send_on_open_(status.into_raw(), node_info.as_mut())?;
            let (inner, _is_terminated) = request_stream.into_inner();
            // It's safe to unwrap here because inner is clearly the only Arc reference left.
            Ok(Arc::try_unwrap(inner).unwrap().into_channel())
        };

        let flags = match new_connection_validate_flags(flags, mode) {
            Ok(updated) => updated,
            Err(status) => {
                if let Ok(channel) = describe(channel, Err(status)) {
                    let _ = channel.close_with_epitaph(status);
                }
                return;
            }
        };

        debug_assert!(flags.contains(fio::OpenFlags::RIGHT_READABLE));

        if let Ok(channel) = describe(channel, Ok(())) {
            (self.open)(scope, channel);
        }
    }
}

impl DirectoryEntry for Service {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        if !path.is_empty() {
            // See comment at the beginning of [`Service::open_as_service`].
            if flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
                send_on_open_with_error(flags, server_end, Status::NOT_DIR);
            }
            return;
        }

        if flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
            self.open_as_node(scope, flags, mode, server_end);
        } else {
            self.open_as_service(scope, flags, mode, server_end);
        }
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Service)
    }
}
