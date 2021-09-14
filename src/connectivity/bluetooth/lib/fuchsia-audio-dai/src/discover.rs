// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use files_async;
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use std::path::Path;

use crate::DigitalAudioInterface;

const DAI_DEVICE_DIR: &str = "/dev/class/dai";

/// Finds any DAI devices, connects to any that are available and provides
pub async fn find_devices() -> Result<Vec<DigitalAudioInterface>, Error> {
    // Connect to the component's environment.
    let directory_proxy = open_directory_in_namespace(DAI_DEVICE_DIR, OPEN_RIGHT_READABLE)?;
    find_devices_internal(directory_proxy).await
}

async fn find_devices_internal(
    directory_proxy: fidl_fuchsia_io::DirectoryProxy,
) -> Result<Vec<DigitalAudioInterface>, Error> {
    let files = files_async::readdir(&directory_proxy).await?;

    let paths: Vec<_> =
        files.iter().map(|file| Path::new(DAI_DEVICE_DIR).join(&file.name)).collect();
    let devices = paths.iter().map(|path| DigitalAudioInterface::new(&path)).collect();

    Ok(devices)
}

#[cfg(test)]
mod tests {
    use {
        anyhow::Error,
        fidl_fuchsia_io2 as fio2, fuchsia,
        fuchsia_component_test::{
            builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
            mock::{Mock, MockHandles},
        },
        futures::{channel::mpsc, SinkExt, StreamExt},
        realmbuilder_mock_helpers::mock_dev,
    };

    use super::*;
    use crate::test::mock_dai_dev_with_io_devices;

    #[fuchsia::test]
    async fn test_env_dir_is_not_found() {
        let _ = find_devices().await.expect_err("find devices okay");
    }

    async fn mock_client(handles: MockHandles, mut sender: mpsc::Sender<()>) -> Result<(), Error> {
        let proxy = handles.clone_from_namespace("dev/class/dai")?;
        let devices = find_devices_internal(proxy).await.expect("should find devices");
        assert_eq!(devices.len(), 2);
        let _ = sender.send(()).await.unwrap();
        Ok(())
    }

    #[fuchsia::test]
    async fn devices_found_from_env() {
        let (device_sender, mut device_receiver) = mpsc::channel(0);
        let mut builder = RealmBuilder::new().await.expect("Failed to create test realm builder");

        // Add a mock that provides the dev/ directory with one input and output device.
        let _ = builder
            .add_eager_component(
                "mock-dev",
                ComponentSource::Mock(Mock::new({
                    move |mock_handles: MockHandles| {
                        Box::pin(mock_dev(
                            mock_handles,
                            mock_dai_dev_with_io_devices(
                                "input1".to_string(),
                                "output1".to_string(),
                            ),
                        ))
                    }
                })),
            )
            .await
            .expect("Failed adding mock /dev provider to topology");

        // Add a mock that represents a client trying to discover DAI devices.
        let _ = builder
            .add_eager_component(
                "mock-client",
                ComponentSource::Mock(Mock::new(move |mock_handles: MockHandles| {
                    let s = device_sender.clone();
                    Box::pin(mock_client(mock_handles, s.clone()))
                })),
            )
            .await
            .expect("Failed adding mock client to topology");

        // Give client access to dev/
        let _ = builder
            .add_route(CapabilityRoute {
                capability: Capability::directory("dev-dai", DAI_DEVICE_DIR, fio2::RW_STAR_DIR),
                source: RouteEndpoint::component("mock-dev".to_string()),
                targets: vec![RouteEndpoint::component("mock-client".to_string())],
            })
            .expect("Failed adding route for dai device directory");

        let test_topology = builder.build().create().await.unwrap();

        let _ = test_topology.root.connect_to_binder().unwrap();

        let _ = device_receiver.next().await.expect("should receive devices");
    }
}
