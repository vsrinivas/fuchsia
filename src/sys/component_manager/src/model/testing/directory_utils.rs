// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for working with fuchsia VFS objects.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fuchsia_vfs_pseudo_fs::directory::entry::{DirectoryEntry, EntryInfo},
    std::{
        future::Future,
        pin::Pin,
        sync::{Arc, Mutex},
        task::{Context, Poll},
    },
    void::Void,
};

/// Thread-safe (Sync + Send) and Cloneable wrapper around a `DirectoryEntry`.
///
/// This allows a `DirectoryEntry` to be shared amongst multiple threads,
/// and also cloned to be used multiple times.
///
/// `open` and `poll` calls on the underlying `DirectoryEntry` object
/// are synchronised using an internal mutex.
///
// TODO(fxb/37392): This struct may be unnecessary after we've moved across to the thread-safe
// version of the VFS libraries.
#[derive(Clone)]
pub struct SyncDirectoryEntry {
    entry: Arc<std::sync::Mutex<Pin<Box<dyn DirectoryEntry>>>>,
}

impl SyncDirectoryEntry {
    pub fn new(entry: impl DirectoryEntry + 'static) -> SyncDirectoryEntry {
        Self { entry: Arc::new(Mutex::new(Box::pin(entry))) }
    }
}

impl DirectoryEntry for SyncDirectoryEntry {
    fn open(
        &mut self,
        flags: u32,
        mode: u32,
        path: &mut dyn Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) {
        self.entry.lock().unwrap().open(flags, mode, path, server_end)
    }

    fn entry_info(&self) -> EntryInfo {
        self.entry.lock().unwrap().entry_info()
    }
}

impl Future for SyncDirectoryEntry {
    type Output = Void;
    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        self.entry.lock().unwrap().as_mut().poll(ctx)
    }
}

impl futures::future::FusedFuture for SyncDirectoryEntry {
    fn is_terminated(&self) -> bool {
        self.entry.lock().unwrap().as_ref().is_terminated()
    }
}
