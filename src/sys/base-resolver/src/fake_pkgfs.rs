// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A very much incomplete implementation of fuchsia.io for the purpose of mocking
//! pkgfs.
//!
//! The reason the standard `vfs` library can't be used is because it does not support
//! the OPEN_RIGHT_EXECUTABLE. Attempts to add this support to `vfs` break the current
//! POSIX support in that library.
//!
//! TODO(fxbug.dev/46491): Remove this once `vfs` supports OPEN_FLAG_EXECUTABLE.

use {
    fidl::endpoints::ServerEnd,
    fidl::prelude::*,
    fidl_fuchsia_io::{
        self as fio, DirectoryMarker, DirectoryObject, DirectoryProxy, DirectoryRequest, NodeInfo,
        NodeMarker,
    },
    fuchsia_async::Task,
    fuchsia_zircon::{Status, Vmo},
    futures::prelude::*,
    std::{collections::HashMap, path::Path, sync::Arc},
    vfs::directory::entry::DirectoryEntry,
};

/// All nodes (directories, files, etc) implement this.
pub trait Entry {
    fn open(self: Arc<Self>, flags: u32, mode: u32, path: &str, object: ServerEnd<NodeMarker>);
}

/// The implementation of a mock directory.
pub struct MockDir {
    subdirs: HashMap<String, Arc<dyn Entry>>,
}

impl MockDir {
    pub fn new() -> Self {
        MockDir { subdirs: HashMap::new() }
    }

    pub fn add_entry(mut self, name: &str, entry: Arc<dyn Entry>) -> Self {
        self.subdirs.insert(name.into(), entry);
        self
    }

    async fn serve(self: Arc<Self>, object: ServerEnd<DirectoryMarker>) {
        let mut stream = object.into_stream().unwrap();
        // If we can't send an event, the client closed their connection. This is not an error.
        let _ = stream.control_handle().send_on_open_(
            Status::OK.into_raw(),
            Some(&mut NodeInfo::Directory(DirectoryObject {})),
        );
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                DirectoryRequest::Open { flags, mode, path, object, .. } => {
                    self.clone().open(flags, mode, &path, object);
                }
                DirectoryRequest::Clone { flags, object, .. } => {
                    self.clone().open(flags, fio::MODE_TYPE_DIRECTORY, ".", object);
                }
                _ => panic!("unsupported request"),
            }
        }
    }
}

impl Entry for MockDir {
    fn open(self: Arc<Self>, flags: u32, mode: u32, path: &str, object: ServerEnd<NodeMarker>) {
        let path = Path::new(path);
        let mut path_iter = path.iter();
        let segment = if let Some(segment) = path_iter.next() {
            if let Some(segment) = segment.to_str() {
                segment
            } else {
                send_error(object, Status::NOT_FOUND);
                return;
            }
        } else {
            "."
        };
        if segment == "." {
            Task::local(self.clone().serve(ServerEnd::new(object.into_channel()))).detach();
            return;
        }
        if let Some(entry) = self.subdirs.get(segment) {
            entry.clone().open(flags, mode, path_iter.as_path().to_str().unwrap(), object);
        } else {
            send_error(object, Status::NOT_FOUND);
        }
    }
}

impl Entry for DirectoryProxy {
    fn open(self: Arc<Self>, flags: u32, mode: u32, path: &str, object: ServerEnd<NodeMarker>) {
        let _ = DirectoryProxy::open(&*self, flags, mode, path, object);
    }
}

pub struct MockFile {
    file: Arc<dyn DirectoryEntry>,
}

impl MockFile {
    pub fn new(contents: Vec<u8>) -> Self {
        Self {
            file: vfs::file::vmo::read_only(move || {
                let contents = contents.clone();
                async move {
                    let capacity = contents.len() as u64;
                    let vmo = Vmo::create(capacity)?;
                    vmo.write(&contents, 0)?;
                    Ok(vfs::file::vmo::asynchronous::NewVmo { vmo, size: capacity, capacity })
                }
            }),
        }
    }
}

impl Entry for MockFile {
    fn open(self: Arc<Self>, flags: u32, mode: u32, path: &str, object: ServerEnd<NodeMarker>) {
        if !path.is_empty() {
            send_error(object, Status::BAD_PATH);
            return;
        }
        self.file.clone().open(
            vfs::execution_scope::ExecutionScope::new(),
            flags,
            mode,
            vfs::path::Path::dot(),
            object.into_channel().into(),
        );
    }
}

fn send_error(object: ServerEnd<NodeMarker>, status: Status) {
    let stream = object.into_stream().expect("failed to create stream");
    let control_handle = stream.control_handle();
    let _ = control_handle.send_on_open_(status.into_raw(), None);
    control_handle.shutdown_with_epitaph(status);
}
