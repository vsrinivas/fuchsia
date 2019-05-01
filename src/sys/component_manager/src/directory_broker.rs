// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, DIRENT_TYPE_SERVICE, INO_UNKNOWN},
    fuchsia_vfs_pseudo_fs as fvfs,
    fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry,
    //fuchsia_zircon::{self as zx, Status},
    futures::{future::FusedFuture, task::Context, Future, Poll},
    std::pin::Pin,
    void::Void,
};

// TODO(ZX-3606): move this into the pseudo dir fs crate.
/// DirectoryBroker exists to hold a slot in a fuchsia_vfs_pseudo_fs directory and proxy open
/// requests. A DirectoryBroker holds a closure provided at creation time, and whenever an open
/// request for this directory entry is received the given ServerEnd is passed into the closure,
/// which will presumably make an open request somewhere else and forward on the ServerEnd.
pub struct DirectoryBroker {
    route_service: Box<FnMut(ServerEnd<NodeMarker>) + Send>,
    entry_info: fvfs::directory::entry::EntryInfo,
}

impl DirectoryBroker {
    /// new_service_broker will create a new DirectoryBroker to proxy service requests. Whenever an
    /// open call is received the given closure is called with the new ServerEnd. If the entry_info
    /// of this node is inspected, it will tell the caller that it is a service node.
    pub fn new_service_broker(
        route_service: Box<FnMut(ServerEnd<NodeMarker>) + Send>,
    ) -> DirectoryBroker {
        return DirectoryBroker {
            route_service,
            entry_info: fvfs::directory::entry::EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_SERVICE),
        };
    }
}
impl DirectoryEntry for DirectoryBroker {
    fn open(
        &mut self,
        _flags: u32,
        _mode: u32,
        path: &mut Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if let Some(_) = path.next() {
            // Services do not support nested paths, so we just are going to close server_end.
            // It will be closed by the destructor as it will go out of scope.
            // TODO: we probably want to support nested paths
            return;
        }
        (self.route_service)(server_end);
    }
    fn entry_info(&self) -> fvfs::directory::entry::EntryInfo {
        return fvfs::directory::entry::EntryInfo::new(
            self.entry_info.inode(),
            self.entry_info.type_(),
        );
    }
}
impl FusedFuture for DirectoryBroker {
    fn is_terminated(&self) -> bool {
        // TODO: ibobyr says:
        //     As this kind of service is special, it can forward `is_terminated` to the contained
        //     proxy, via the EventStreams, but for now "true" should work as well.
        true
    }
}
impl Future for DirectoryBroker {
    type Output = Void;
    fn poll(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}
