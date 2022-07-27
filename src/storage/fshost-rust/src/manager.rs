// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{device::Device, matcher, matcher::Environment, service, watcher},
    anyhow::{format_err, Error},
    async_trait::async_trait,
    futures::{channel::mpsc, StreamExt},
    std::path::Path,
};

pub struct Manager {
    shutdown_rx: mpsc::Receiver<service::FshostShutdownResponder>,
    matcher: matcher::Matchers,
}

struct Env();

#[async_trait]
impl Environment for Env {
    async fn attach_driver(&self, _device: &dyn Device, _driver_path: &str) -> Result<(), Error> {
        todo!();
    }
    async fn mount_blobfs(&self, _device: &dyn Device) -> Result<(), Error> {
        todo!();
    }
    async fn mount_data(&self, _device: &dyn Device) -> Result<(), Error> {
        todo!();
    }
}

impl Manager {
    pub fn new(
        shutdown_rx: mpsc::Receiver<service::FshostShutdownResponder>,
        config: fshost_config::Config,
    ) -> Self {
        Manager { shutdown_rx, matcher: matcher::Matchers::new(config) }
    }

    /// The main loop of fshost. Watch for new devices, match them against filesystems we expect,
    /// and then launch them appropriately.
    pub async fn device_handler(&mut self) -> Result<service::FshostShutdownResponder, Error> {
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

            async fn path(device: &impl Device) -> &str {
                device.topological_path().await.ok().and_then(Path::to_str).unwrap_or("?")
            }

            match self.matcher.match_device(device.as_ref(), &Env()).await {
                Ok(true) => {}
                Ok(false) => log::info!("Ignored device: `{}`", path(&*device).await),
                Err(e) => {
                    log::error!("Failed to match device `{}`: {}", path(&*device).await, e)
                }
            }
        }
    }

    pub async fn shutdown(self) -> Result<(), Error> {
        Ok(())
    }
}
