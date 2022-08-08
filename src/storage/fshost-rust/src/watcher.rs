// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fuchsia_watch::{watch, PathEvent},
    futures::stream::StreamExt,
};

const DEV_CLASS_BLOCK: &'static str = "/dev/class/block";

/// Get a stream of incoming block devices to possibly handle.
pub async fn block_watcher() -> Result<impl futures::Stream<Item = String>> {
    Ok(watch(DEV_CLASS_BLOCK).await?.filter_map(|event| async {
        match event {
            PathEvent::Added(path, _) => Some(path),
            PathEvent::Existing(path, _) => Some(path),
            PathEvent::Removed(_) => None,
        }
        .map(|p| p.into_os_string().into_string().unwrap())
    }))
}
