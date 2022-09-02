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
        config: fshost_config::Config,
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
                    log::error!("Unable to create device: {}", e);
                    continue;
                }
                Ok(device) => device,
            };

            // let Manager { matcher, environment } = &mut self;

            match self.matcher.match_device(&mut device, &mut self.environment).await {
                Ok(true) => {}
                Ok(false) => {
                    log::info!("Ignored device: `{}`", device.topological_path());
                }
                Err(e) => {
                    log::error!("Failed to match device `{}`: {}", device.topological_path(), e)
                }
            }
        }
    }

    pub async fn shutdown(self) -> Result<(), Error> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::Manager,
        crate::{
            config::default_config, device::constants::BLOBFS_TYPE_GUID,
            environment::FshostEnvironment, service, watcher,
        },
        device_watcher::recursive_wait_and_open_node,
        fidl::endpoints::Proxy,
        fidl_fuchsia_device::ControllerProxy,
        fidl_fuchsia_io as fio,
        fs_management::Blobfs,
        fuchsia_async as fasync,
        fuchsia_component::client::connect_to_protocol_at_path,
        futures::{channel::mpsc, select, FutureExt},
        ramdevice_client::RamdiskClient,
        std::path::Path,
        storage_isolated_driver_manager::fvm::{create_fvm_volume, set_up_fvm},
        uuid::Uuid,
    };

    fn dev() -> fio::DirectoryProxy {
        connect_to_protocol_at_path::<fio::DirectoryMarker>("/dev")
            .expect("connect_to_protocol_at_path failed")
    }

    async fn ramdisk() -> RamdiskClient {
        recursive_wait_and_open_node(&dev(), "sys/platform/00:00:2d/ramctl")
            .await
            .expect("recursive_wait_and_open_node failed");
        RamdiskClient::create(512, 1 << 16).unwrap()
    }

    #[ignore]
    #[fasync::run_singlethreaded(test)]
    async fn test_mount_blobfs() {
        println!("making the ramdisk");
        let ramdisk = ramdisk().await;
        let ramdisk_path = ramdisk.get_path();
        println!("formatting the disk with fvm");
        {
            let volume_manager_proxy = set_up_fvm(Path::new(ramdisk_path), 32 * 1024)
                .await
                .expect("format_for_fvm failed");
            create_fvm_volume(
                &volume_manager_proxy,
                "blobfs",
                &BLOBFS_TYPE_GUID,
                Uuid::new_v4().as_bytes(),
                None,
                0,
            )
            .await
            .expect("create_fvm_volume failed");
            let blobfs_path = format!("{}/fvm/blobfs-p-1/block", ramdisk_path);
            println!("and blobfs");
            recursive_wait_and_open_node(&dev(), blobfs_path.strip_prefix("/dev/").unwrap())
                .await
                .expect("recursive_wait_and_open_node failed");
            let blobfs = Blobfs::new(&blobfs_path).expect("new failed");
            println!("formatting blobfs");
            blobfs.format().await.expect("format failed");

            println!("and reseting back to normal");
            let device_proxy = ControllerProxy::new(volume_manager_proxy.into_channel().unwrap());
            device_proxy
                .schedule_unbind()
                .await
                .expect("schedule unbind fidl failed")
                .expect("schedule_unbind failed");
        }
        println!("setting up the things needed for the manager");
        let (_shutdown_tx, shutdown_rx) = mpsc::channel::<service::FshostShutdownResponder>(1);
        let mut env = FshostEnvironment::new();
        let blobfs_root = env.blobfs_root().expect("blobfs_root failed");
        let mut manager = Manager::new(shutdown_rx, default_config(), env);
        println!("creating the watcher");
        let (_watcher, device_stream) =
            watcher::Watcher::new().await.expect("starting watcher failed");
        println!("running the device handler and waiting for blobfs");
        select! {
            _ = manager.device_handler(device_stream).fuse() => unreachable!(),
            result = blobfs_root.describe().fuse() => { result.expect("describe failed"); }
        }
    }
}
