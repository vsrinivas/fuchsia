// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module defines a watcher that subscribes to the device filesystem and
//! produces a stream of messages when bt-host devices are added or removed from
//! the system

use {
    anyhow::Error,
    fuchsia_bluetooth::constants::HOST_DEVICE_DIR,
    fuchsia_vfs_watcher::{self as vfs_watcher, WatchEvent, WatchMessage},
    futures::{FutureExt, Stream, TryStreamExt},
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    log::warn,
    std::{
        io,
        path::{Path, PathBuf},
    },
};

pub enum AdapterEvent {
    AdapterAdded(PathBuf),
    AdapterRemoved(PathBuf),
}

/// Watch the VFS for host adapter devices being added or removed, and produce
/// a stream of AdapterEvent messages
pub fn watch_hosts() -> impl Stream<Item = Result<AdapterEvent, Error>> {
    async {
        let directory = open_directory_in_namespace(HOST_DEVICE_DIR, OPEN_RIGHT_READABLE).unwrap();
        let watcher = vfs_watcher::Watcher::new(directory)
            .await
            .expect("Cannot open vfs watcher for bt-host device path");
        watcher.try_filter_map(as_adapter_msg).err_into()
    }
    .flatten_stream()
}

async fn as_adapter_msg(msg: WatchMessage) -> Result<Option<AdapterEvent>, io::Error> {
    use self::AdapterEvent::*;

    let path = Path::new(HOST_DEVICE_DIR).join(&msg.filename);
    Ok(match msg.event {
        WatchEvent::EXISTING | WatchEvent::ADD_FILE => Some(AdapterAdded(path)),
        WatchEvent::REMOVE_FILE => Some(AdapterRemoved(path)),
        WatchEvent::IDLE => None,
        e => {
            warn!("Unrecognized host watch event: {:?}", e);
            None
        }
    })
}
