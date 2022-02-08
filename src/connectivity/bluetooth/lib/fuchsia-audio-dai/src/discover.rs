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
        fidl_fuchsia_io as fio, fuchsia,
        fuchsia_component_test::new::{
            Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Route,
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

    async fn mock_client(
        handles: LocalComponentHandles,
        mut sender: mpsc::Sender<()>,
    ) -> Result<(), Error> {
        let proxy = handles.clone_from_namespace("dev/class/dai")?;
        let devices = find_devices_internal(proxy).await.expect("should find devices");
        assert_eq!(devices.len(), 2);
        let _ = sender.send(()).await.unwrap();
        Ok(())
    }

    #[fuchsia::test]
    async fn devices_found_from_env() {
        let (device_sender, mut device_receiver) = mpsc::channel(0);
        let builder = RealmBuilder::new().await.expect("Failed to create test realm builder");

        // Add a mock that provides the dev/ directory with one input and output device.
        let mock_dev = builder
            .add_local_child(
                "mock-dev",
                move |handles: LocalComponentHandles| {
                    Box::pin(mock_dev(
                        handles,
                        mock_dai_dev_with_io_devices("input1".to_string(), "output1".to_string()),
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await
            .expect("Failed adding mock /dev provider to topology");

        // Add a mock that represents a client trying to discover DAI devices.
        let mock_client = builder
            .add_local_child(
                "mock-client",
                move |handles: LocalComponentHandles| {
                    let s = device_sender.clone();
                    Box::pin(mock_client(handles, s.clone()))
                },
                ChildOptions::new().eager(),
            )
            .await
            .expect("Failed adding mock client to topology");

        // Give client access to dev/
        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("dev-dai")
                            .path(DAI_DEVICE_DIR)
                            .rights(fio::RW_STAR_DIR),
                    )
                    .from(&mock_dev)
                    .to(&mock_client),
            )
            .await
            .expect("Failed adding route for dai device directory");

        let _test_topology = builder.build().await.unwrap();

        let _ = device_receiver.next().await.expect("should receive devices");
    }
}
