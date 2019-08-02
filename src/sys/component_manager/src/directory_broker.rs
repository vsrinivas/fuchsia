// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryProxy, NodeMarker, DIRENT_TYPE_SERVICE, INO_UNKNOWN},
    fuchsia_vfs_pseudo_fs as fvfs,
    fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry,
    futures::{future::FusedFuture, task::Context, Future, Poll},
    std::pin::Pin,
    void::Void,
};

pub type RoutingFn = Box<dyn FnMut(u32, u32, String, ServerEnd<NodeMarker>) + Send>;

// TODO(ZX-3606): move this into the pseudo dir fs crate.
/// DirectoryBroker exists to hold a slot in a fuchsia_vfs_pseudo_fs directory and proxy open
/// requests. A DirectoryBroker holds a closure provided at creation time, and whenever an open
/// request for this directory entry is received the given ServerEnd is passed into the closure,
/// which will presumably make an open request somewhere else and forward on the ServerEnd.
pub struct DirectoryBroker {
    /// The parameters are as follows:
    ///  flags: u32
    ///  mode: u32
    ///  relative_path: String
    ///  server_end: ServerEnd<NodeMarker>
    route_open: RoutingFn,
    entry_info: fvfs::directory::entry::EntryInfo,
}

impl DirectoryBroker {
    /// new will create a new DirectoryBroker to forward directory open requests.
    pub fn new(route_open: RoutingFn) -> Self {
        return DirectoryBroker {
            route_open,
            entry_info: fvfs::directory::entry::EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_SERVICE),
        };
    }

    pub fn from_directory_proxy(dir: DirectoryProxy) -> DirectoryBroker {
        Self::new(Box::new(
            move |flags: u32,
                  mode: u32,
                  relative_path: String,
                  server_end: ServerEnd<NodeMarker>| {
                // If we want to open the 'dir' directory directly, then call clone.
                // Otherwise, pass long the remaining 'relative_path' to the component
                // hosting the out directory to resolve.
                if !relative_path.is_empty() {
                    // TODO(fsamuel): Currently DirectoryEntry::open does not return
                    // a Result so we cannot propagate this error up. We probably
                    // want to change that.
                    let _ = dir.open(flags, mode, &relative_path, server_end);
                } else {
                    let _ = dir.clone(flags, server_end);
                }
            },
        ))
    }
}

impl DirectoryEntry for DirectoryBroker {
    fn open(
        &mut self,
        flags: u32,
        mode: u32,
        path: &mut dyn Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let relative_path = path.collect::<Vec<&str>>().join("/");
        (self.route_open)(flags, mode, relative_path, server_end);
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
