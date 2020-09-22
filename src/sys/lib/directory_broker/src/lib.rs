// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryProxy, NodeMarker, DIRENT_TYPE_SERVICE, INO_UNKNOWN},
    parking_lot::Mutex,
    std::sync::Arc,
    vfs as fvfs,
    vfs::{
        directory::entry::DirectoryEntry, directory::entry::EntryInfo,
        execution_scope::ExecutionScope, path::Path,
    },
};

pub type RoutingFn = Box<dyn FnMut(u32, u32, String, ServerEnd<NodeMarker>) + Send + Sync>;

// TODO(fxbug.dev/33398): move this into the pseudo dir fs crate.
/// DirectoryBroker exists to hold a slot in a fuchsia_vfs_pseudo_fs directory and proxy open
/// requests. A DirectoryBroker holds a closure provided at creation time, and whenever an open
/// request for this directory entry is received the given ServerEnd is passed into the closure,
/// which will presumably make an open request somewhere else and forward on the ServerEnd.
pub struct DirectoryBroker {
    inner: Mutex<Inner>,
}

struct Inner {
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
    pub fn new(route_open: RoutingFn) -> Arc<DirectoryBroker> {
        return Arc::new(DirectoryBroker {
            inner: Mutex::new(Inner {
                route_open,
                entry_info: EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_SERVICE),
            }),
        });
    }

    pub fn from_directory_proxy(dir: DirectoryProxy) -> Arc<DirectoryBroker> {
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
                    // TODO(fsamuel): For some reason, DirectoryProxy receives an
                    // empty string as a relative path. That is unsupported by open.
                    // Why does it receive an empty string? Shouldn't it be "."?
                    // Or should directories support empty strings as relative paths?
                    // For now, change the empty string to ".", but investigate further.
                    let _ = dir.open(flags, mode, ".", server_end);
                }
            },
        ))
    }
}

impl DirectoryEntry for DirectoryBroker {
    fn open(
        self: Arc<Self>,
        _scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let mut this = self.inner.lock();
        (&mut this.route_open)(flags, mode, path.into_string(), server_end);
    }

    fn entry_info(&self) -> fvfs::directory::entry::EntryInfo {
        self.inner.lock().entry_info.clone()
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}
