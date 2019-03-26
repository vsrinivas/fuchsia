// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::DirectoryRequestStream,
    futures::{
        stream::{Stream, StreamExt, StreamFuture},
        task::Waker,
        Poll,
    },
    std::{default::Default, pin::Pin},
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
    pub fn into_stream_future(
        requests: DirectoryRequestStream,
        flags: u32,
    ) -> StreamFuture<DirectoryConnection<TraversalPosition>> {
        (DirectoryConnection { requests, flags, seek: Default::default() }).into_future()
    }
}

impl<TraversalPosition> Stream for DirectoryConnection<TraversalPosition>
where
    TraversalPosition: Default,
{
    // We are just proxying the DirectoryRequestStream requests.
    type Item = <DirectoryRequestStream as Stream>::Item;

    fn poll_next(mut self: Pin<&mut Self>, lw: &Waker) -> Poll<Option<Self::Item>> {
        self.requests.poll_next_unpin(lw)
    }
}

impl<TraversalPosition> Unpin for DirectoryConnection<TraversalPosition> where
    TraversalPosition: Default
{
}
