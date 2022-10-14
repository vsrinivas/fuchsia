// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        device::{BlockDevice, Device},
        environment::Environment,
        matcher, service,
    },
    anyhow::{format_err, Error},
    futures::{channel::mpsc, StreamExt},
};

pub struct Manager<E> {
    shutdown_rx: mpsc::Receiver<service::FshostShutdownResponder>,
    matcher: matcher::Matchers,
    environment: E,
}

impl<E: Environment> Manager<E> {
    pub fn new(
        shutdown_rx: mpsc::Receiver<service::FshostShutdownResponder>,
        config: &fshost_config::Config,
        environment: E,
    ) -> Self {
        Manager { shutdown_rx, matcher: matcher::Matchers::new(config), environment }
    }

    /// The main loop of fshost. Watch for new devices, match them against filesystems we expect,
    /// and then launch them appropriately.
    pub async fn device_handler(
        &mut self,
        device_stream: impl futures::Stream<Item = String>,
    ) -> Result<service::FshostShutdownResponder, Error> {
        let mut device_stream = Box::pin(device_stream).fuse();
        loop {
            // Wait for the next device to come in, or the shutdown signal to arrive.
            let device_path = futures::select! {
                responder = self.shutdown_rx.next() => {
                    let responder = responder
                        .ok_or_else(|| format_err!("shutdown signal stream ended unexpectedly"))?;
                    return Ok(responder);
                },
                maybe_device = device_stream.next() => {
                    if let Some(device_path) = maybe_device {
                        device_path
                    } else {
                        anyhow::bail!("block watcher returned none unexpectedly");
                    }
                },
            };

            let mut device = match BlockDevice::new(device_path).await {
                Err(e) => {
                    tracing::error!("Unable to create device: {}", e);
                    continue;
                }
                Ok(device) => device,
            };

            tracing::info!(path = %device.topological_path(), "Matching device");

            match self.matcher.match_device(&mut device, &mut self.environment).await {
                Ok(true) => {}
                Ok(false) => tracing::info!(path = %device.topological_path(), "Ignored device"),
                Err(e) => {
                    tracing::error!(
                        path = %device.topological_path(),
                        "Failed to match device: {}",
                        e
                    );
                }
            }
        }
    }

    pub async fn shutdown(self) -> Result<(), Error> {
        Ok(())
    }
}
