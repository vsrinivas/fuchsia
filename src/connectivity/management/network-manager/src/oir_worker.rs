// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Online Insertion and Removal worker.

use {
    anyhow::{format_err, Context as _},
    fidl_fuchsia_hardware_ethernet_ext::is_physical,
    futures::stream::{Stream, StreamExt as _, TryStreamExt as _},
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    network_manager_core::oir::OIRInfo,
    std::fs,
    std::os::unix::io::AsRawFd,
    std::path,
};

/// A node that represents the directory it is in.
///
/// `/dir` and `/dir/.` point to the same directory.
const THIS_DIRECTORY: &str = ".";

fn ethdir_path() -> std::path::PathBuf {
    let options: crate::Opt = argh::from_env();
    path::Path::new(&options.dev_path).join("class/ethernet")
}

async fn device_found(filename: &std::path::PathBuf) -> Result<Option<OIRInfo>, anyhow::Error> {
    let file_path = ethdir_path().join(filename);
    let device = fs::File::open(&file_path)
        .with_context(|| format!("could not open {}", file_path.display()))?;
    let topological_path = fdio::device_get_topo_path(&device)
        .with_context(|| format!("fdio::device_get_topo_path({})", file_path.display()))?;
    let device_fd = device.as_raw_fd();
    let mut client = 0;
    // Safe because we're passing a valid fd.
    fuchsia_zircon::Status::ok(unsafe {
        fdio::fdio_sys::fdio_get_service_handle(device_fd, &mut client)
    })
    .with_context(|| {
        format!("fuchsia_zircon::sys::fdio_get_service_handle({})", file_path.display())
    })?;
    // Safe because we checked the return status above.
    let client = fuchsia_zircon::Channel::from(unsafe { fuchsia_zircon::Handle::from_raw(client) });

    let ethernet_device =
        fidl::endpoints::ClientEnd::<fidl_fuchsia_hardware_ethernet::DeviceMarker>::new(client)
            .into_proxy()?;

    if let Ok(device_info) = ethernet_device.get_info().await {
        let device_info: fidl_fuchsia_hardware_ethernet_ext::EthernetInfo = device_info.into();
        let device_channel = ethernet_device
            .into_channel()
            .map_err(|fidl_fuchsia_hardware_ethernet::DeviceProxy { .. }| {
                anyhow::anyhow!("failed to convert device proxy into channel")
            })?
            .into_zx_channel();

        info!("Device found: topo_path {} info {:?}", topological_path, device_info);
        if is_physical(device_info.features) {
            return Ok(Some(OIRInfo {
                action: network_manager_core::oir::Action::ADD,
                file_path: file_path.to_str().unwrap().to_string(),
                topological_path,
                device_information: Some(device_info),
                device_channel: Some(device_channel),
            }));
        } else {
            info!("device not physical {:?}", device_info);
        }
    }

    Ok(None)
}

fn device_removed(topological_path: &std::path::PathBuf) -> OIRInfo {
    info!("removing device topo path {:?}", topological_path);
    OIRInfo {
        action: network_manager_core::oir::Action::REMOVE,
        topological_path: topological_path.to_str().unwrap().to_string(),
        file_path: "".to_string(),
        device_information: None,
        device_channel: None,
    }
}

/// Returns a `Stream` that yields `OIRInfo`s in response to events from the
/// device filesystem.
pub(super) async fn new_stream(
) -> Result<impl Stream<Item = Result<OIRInfo, anyhow::Error>>, anyhow::Error> {
    let path = ethdir_path();
    let path_as_str = path.to_str().ok_or_else(|| {
        format_err!(
            "path requested for watch is non-utf8 and our non-blocking directory apis require utf8 paths: {:?}.",
            path
        )
    })?;
    debug!("device path: {}", path_as_str);
    let dir_proxy = open_directory_in_namespace(path_as_str, OPEN_RIGHT_READABLE)?;
    let watcher = fuchsia_vfs_watcher::Watcher::new(dir_proxy)
        .await
        .with_context(|| format!("could not watch {:?}", path))?;

    Ok(watcher
        .map(|r| r.context("getting next VFS event from"))
        .try_filter_map(|fuchsia_vfs_watcher::WatchMessage { event, filename }| async move {
            info!("received {:?} event for filename: {:?}", event, filename);

            if filename == path::PathBuf::from(THIS_DIRECTORY) {
                debug!("skipping filename = {}", filename.display());
                return Ok(None);
            }

            match event {
                fuchsia_vfs_watcher::WatchEvent::ADD_FILE
                | fuchsia_vfs_watcher::WatchEvent::EXISTING => Ok(device_found(&filename).await?),
                fuchsia_vfs_watcher::WatchEvent::IDLE => Ok(None),
                fuchsia_vfs_watcher::WatchEvent::REMOVE_FILE => Ok(Some(device_removed(&filename))),
                event => {
                    warn!("received unknown event {:?} for filename: {:?}", event, filename);
                    Ok(None)
                }
            }
        })
        .map(move |r| r.with_context(|| format!("watching {:?}", path))))
}
