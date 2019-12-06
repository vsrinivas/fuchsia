// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{common::send_on_open_with_error, directory::common::new_connection_validate_flags};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryObject, DirectoryRequestStream, NodeInfo, NodeMarker,
        OPEN_FLAG_DESCRIBE,
    },
    fuchsia_zircon::Status,
    futures::stream::{Stream, StreamExt, StreamFuture},
    std::{
        default::Default,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// Represents a FIDL connection to a directory.  A single directory may contain multiple
/// connections.  An instances of the DirectoryConnection will also hold any state that is
/// "per-connection".  Currently that would be the access flags and the seek position.
pub struct DirectoryConnection<TraversalPosition>
where
    TraversalPosition: Default,
{
    requests: DirectoryRequestStream,

    /// Flags set on this connection when it was opened or cloned.
    pub flags: u32,

    /// Seek position for this connection to the directory.  We just store the element that was
    /// returned last by ReadDirents for this connection.  Next call will look for the next element
    /// in alphabetical order and resume from there.
    ///
    /// An alternative is to use an intrusive tree to have a dual index in both names and IDs that
    /// are assigned to the entries in insertion order.  Then we can store an ID instead of the
    /// full entry name.  This is what the C++ version is doing currently.
    ///
    /// It should be possible to do the same intrusive dual-indexing using, for example,
    ///
    ///     https://docs.rs/intrusive-collections/0.7.6/intrusive_collections/
    ///
    /// but, as, I think, at least for the pseudo directories, this approach is fine, and it simple
    /// enough.
    pub seek: TraversalPosition,
}

impl<TraversalPosition> DirectoryConnection<TraversalPosition>
where
    TraversalPosition: Default,
{
    /// Initializes a directory connection, checking the flags and sending `OnOpen` event if
    /// necessary.  Returns a [`DirectoryConnection`] object as a [`StreamFuture`], or in case of
    /// an error, sends an appropriate `OnOpen` event (if requested) and returns `None`.
    pub fn connect(
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) -> Option<StreamFuture<DirectoryConnection<TraversalPosition>>> {
        let flags = match new_connection_validate_flags(flags, mode) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return None;
            }
        };

        // As we report all errors on `server_end`, if we failed to send an error in there, there
        // is nowhere to send it to.
        let (requests, control_handle) =
            match ServerEnd::<DirectoryMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()
            {
                Ok((requests, control_handle)) => (requests, control_handle),
                Err(_) => return None,
            };

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            let mut info = NodeInfo::Directory(DirectoryObject);
            match control_handle.send_on_open_(Status::OK.into_raw(), Some(&mut info)) {
                Ok(()) => (),
                Err(_) => return None,
            }
        }

        let conn = DirectoryConnection { requests, flags, seek: Default::default() };
        Some(conn.into_future())
    }
}

impl<TraversalPosition> Stream for DirectoryConnection<TraversalPosition>
where
    TraversalPosition: Default,
{
    // We are just proxying the DirectoryRequestStream requests.
    type Item = <DirectoryRequestStream as Stream>::Item;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.requests.poll_next_unpin(cx)
    }
}

impl<TraversalPosition> Unpin for DirectoryConnection<TraversalPosition> where
    TraversalPosition: Default
{
}
