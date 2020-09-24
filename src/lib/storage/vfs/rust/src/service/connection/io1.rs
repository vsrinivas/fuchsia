// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an individual connection to a file.

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error},
    execution_scope::ExecutionScope,
    service::common::{new_connection_validate_flags, POSIX_READ_WRITE_PROTECTION_ATTRIBUTES},
};

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileMarker, FileRequest, FileRequestStream, NodeAttributes, NodeInfo, NodeMarker, Service,
        INO_UNKNOWN, MODE_TYPE_SERVICE, OPEN_FLAG_DESCRIBE, OPEN_FLAG_NODE_REFERENCE,
    },
    fuchsia_zircon::{
        sys::{ZX_ERR_ACCESS_DENIED, ZX_ERR_NOT_SUPPORTED, ZX_OK},
        Status,
    },
    futures::stream::StreamExt,
};

/// Represents a FIDL connection to a service.
pub struct Connection {
    /// Execution scope this connection and any async operations and connections it creates will
    /// use.
    scope: ExecutionScope,

    /// Wraps a FIDL connection, providing messages coming from the client.
    requests: FileRequestStream,
}

/// Return type for [`handle_request()`] functions.
enum ConnectionState {
    /// Connection is still alive.
    Alive,
    /// Connection have received Node::Close message, it was dropped by the peer, or an error had
    /// occured.  As we do not perform any actions, except for closing our end we do not distiguish
    /// those cases, unlike file and directory connections.
    Closed,
}

impl Connection {
    /// Initialized a NODE_REFERENCE service connection, which will be running in the context of
    /// the specified execution `scope`.  This function will also check the flags and will send the
    /// `OnOpen` event if necessary.
    pub fn create_connection(
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let task = Self::create_connection_task(scope.clone(), flags, mode, server_end);
        // If we failed to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do, but to ignore the open.  `server_end` will be closed when the object will
        // be dropped - there seems to be no error to report there.
        let _ = scope.spawn(Box::pin(task));
    }

    async fn create_connection_task(
        scope: ExecutionScope,
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
            match ServerEnd::<FileMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()
            {
                Ok((requests, control_handle)) => (requests, control_handle),
                Err(_) => {
                    // As we report all errors on `server_end`, if we failed to send an error over
                    // this connection, there is nowhere to send the error to.
                    return;
                }
            };

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            let mut info = NodeInfo::Service(Service {});
            match control_handle.send_on_open_(Status::OK.into_raw(), Some(&mut info)) {
                Ok(()) => (),
                Err(_) => return,
            }
        }

        let handle_requests = Connection { scope: scope.clone(), requests }.handle_requests();
        handle_requests.await;
    }

    async fn handle_requests(mut self) {
        while let Some(request_or_err) = self.requests.next().await {
            match request_or_err {
                Err(_) => {
                    // FIDL level error, such as invalid message format and alike.  Close the
                    // connection on any unexpected error.
                    // TODO: Send an epitaph.
                    break;
                }
                Ok(request) => {
                    match self.handle_request(request).await {
                        Ok(ConnectionState::Alive) => (),
                        Ok(ConnectionState::Closed) | Err(_) => {
                            // Err(_) means a protocol level error.  Close the connection on any
                            // unexpected error.  TODO: Send an epitaph.
                            break;
                        }
                    }
                }
            }
        }
    }

    /// POSIX protection attributes are hard coded, as we are expecting them to be removed from the
    /// io.fidl altogether.
    fn posix_protection_attributes(&self) -> u32 {
        POSIX_READ_WRITE_PROTECTION_ATTRIBUTES
    }

    /// Handle a [`FileRequest`]. This function is responsible for handing all the file operations
    /// that operate on the connection-specific buffer.
    async fn handle_request(&mut self, req: FileRequest) -> Result<ConnectionState, Error> {
        // TODO(fxbug.dev/37419): Remove `allow(unreachable_patterns)` when the bug is done.
        #[allow(unreachable_patterns)]
        match req {
            FileRequest::Clone { flags, object, control_handle: _ } => {
                self.handle_clone(flags, object);
            }
            FileRequest::Close { responder } => {
                responder.send(ZX_OK)?;
                return Ok(ConnectionState::Closed);
            }
            FileRequest::Describe { responder } => {
                let mut info = NodeInfo::Service(Service {});
                responder.send(&mut info)?;
            }
            FileRequest::Sync { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::GetAttr { responder } => {
                let mut attrs = NodeAttributes {
                    mode: MODE_TYPE_SERVICE | self.posix_protection_attributes(),
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                };
                responder.send(ZX_OK, &mut attrs)?;
            }
            FileRequest::SetAttr { flags: _, attributes: _, responder } => {
                // According to zircon/system/fidl/fuchsia-io/io.fidl the only flag that might be
                // modified through this call is OPEN_FLAG_APPEND, and it is not supported by the
                // PseudoFile.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::Read { count: _, responder } => {
                responder.send(ZX_ERR_ACCESS_DENIED, &[])?;
            }
            FileRequest::ReadAt { offset: _, count: _, responder } => {
                responder.send(ZX_ERR_ACCESS_DENIED, &[])?;
            }
            FileRequest::Write { data: _, responder } => {
                responder.send(ZX_ERR_ACCESS_DENIED, 0)?;
            }
            FileRequest::WriteAt { offset: _, data: _, responder } => {
                responder.send(ZX_ERR_ACCESS_DENIED, 0)?;
            }
            FileRequest::Seek { offset: _, start: _, responder } => {
                responder.send(ZX_ERR_ACCESS_DENIED, 0)?;
            }
            FileRequest::Truncate { length: _, responder } => {
                responder.send(ZX_ERR_ACCESS_DENIED)?;
            }
            FileRequest::GetFlags { responder } => {
                responder.send(ZX_OK, OPEN_FLAG_NODE_REFERENCE)?;
            }
            FileRequest::SetFlags { flags: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::GetBuffer { flags: _, responder } => {
                // There is no backing VMO.
                responder.send(ZX_ERR_NOT_SUPPORTED, None)?;
            }
            // TODO(fxbug.dev/37419): Remove default handling after methods landed.
            _ => {}
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_clone(&mut self, flags: u32, server_end: ServerEnd<NodeMarker>) {
        let parent_flags = OPEN_FLAG_NODE_REFERENCE;
        let flags = match inherit_rights_for_clone(parent_flags, flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        Self::create_connection(self.scope.clone(), flags, 0, server_end);
    }
}
