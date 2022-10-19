// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{device::Device, environment::Environment, matcher, service},
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
        device_stream: impl futures::Stream<Item = Box<dyn Device>>,
    ) -> Result<service::FshostShutdownResponder, Error> {
        let mut device_stream = Box::pin(device_stream).fuse();
        loop {
            // Wait for the next device to come in, or the shutdown signal to arrive.
            let mut device = futures::select! {
                responder = self.shutdown_rx.next() => {
                    let responder = responder
                        .ok_or_else(|| format_err!("shutdown signal stream ended unexpectedly"))?;
                    return Ok(responder);
                },
                maybe_device = device_stream.next() => {
                    if let Some(device) = maybe_device {
                        device
                    } else {
                        anyhow::bail!("block watcher returned none unexpectedly");
                    }
                },
            };

            match self.matcher.match_device(device.as_mut(), &mut self.environment).await {
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
