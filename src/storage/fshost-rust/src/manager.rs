// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{matcher, service, watcher},
    anyhow::{format_err, Result},
    futures::{channel::mpsc, StreamExt},
};

pub struct Manager {
    shutdown_rx: mpsc::Receiver<service::FshostShutdownResponder>,
    matcher: matcher::Matcher,
}

impl Manager {
    pub fn new(shutdown_rx: mpsc::Receiver<service::FshostShutdownResponder>) -> Self {
        Manager { shutdown_rx, matcher: matcher::Matcher::new() }
    }

    /// The main loop of fshost. Watch for new devices, match them against filesystems we expect,
    /// and then launch them appropriately.
    pub async fn device_handler(&mut self) -> Result<service::FshostShutdownResponder> {
        let mut block_watcher = Box::pin(watcher::block_watcher().await?).fuse();
        loop {
            // Wait for the next device to come in, or the shutdown signal to arrive.
            let device = futures::select! {
                responder = self.shutdown_rx.next() => {
                    let responder = responder
                        .ok_or_else(|| format_err!("shutdown signal stream ended unexpectedly"))?;
                    return Ok(responder);
                },
                maybe_device = block_watcher.next() => {
                    if let Some(device) = maybe_device {
                        device
                    } else {
                        anyhow::bail!("block watcher returned none unexpectedly");
                    }
                },
            };

            self.matcher.match_device(device).await?;
        }
    }

    pub async fn shutdown(self) -> Result<()> {
        Ok(())
    }
}
