// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::event::Event,
    failure::{self, ResultExt},
    failure::{format_err, Error},
    fuchsia_async as fasync,
    futures::TryStreamExt,
    futures::{channel::mpsc, TryFutureExt},
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    network_manager_core::oir::OIRInfo,
    std::fs,
    std::os::unix::io::AsRawFd,
    std::path,
};

fn ethdir_path() -> std::path::PathBuf {
    let options: crate::Opt = argh::from_env();
    let path = path::Path::new(&options.dev_path).join("class/ethernet");
    info!("device path: {}", path.display());
    path
}

async fn device_found(
    event_chan: &mpsc::UnboundedSender<Event>,
    filename: &std::path::PathBuf,
) -> Result<(), failure::Error> {
    let file_path = ethdir_path().join(filename);
    let device = fs::File::open(&file_path)
        .with_context(|_| format!("could not open {}", file_path.display()))?;
    let topological_path = fdio::device_get_topo_path(&device)
        .with_context(|_| format!("fdio::device_get_topo_path({})", file_path.display()))?;
    let device_fd = device.as_raw_fd();
    let mut client = 0;
    // Safe because we're passing a valid fd.
    fuchsia_zircon::Status::ok(unsafe {
        fdio::fdio_sys::fdio_get_service_handle(device_fd, &mut client)
    })
    .with_context(|_| {
        format!("fuchsia_zircon::sys::fdio_get_service_handle({})", file_path.display())
    })?;
    // Safe because we checked the return status above.
    let client = fuchsia_zircon::Channel::from(unsafe { fuchsia_zircon::Handle::from_raw(client) });

    let ethernet_device =
        fidl::endpoints::ClientEnd::<fidl_fuchsia_hardware_ethernet::DeviceMarker>::new(client)
            .into_proxy()?;

    if let Ok(device_info) = ethernet_device.get_info().await {
        let device_info: fidl_fuchsia_hardware_ethernet_ext::EthernetInfo = device_info.into();
        info!("device {:#?}", ethernet_device);
        let device_channel = ethernet_device
            .into_channel()
            .map_err(|fidl_fuchsia_hardware_ethernet::DeviceProxy { .. }| {
                failure::err_msg("failed to convert device proxy into channel")
            })?
            .into_zx_channel();

        info!("device_channel {:#?}", device_channel);
        info!("device info {:?}", device_info);
        info!("topo_path {:?}", topological_path);
        if device_info.features.is_physical() {
            event_chan.unbounded_send(Event::OIR(OIRInfo {
                action: network_manager_core::oir::Action::ADD,
                file_path: file_path.to_str().unwrap().to_string(),
                topological_path,
                device_information: Some(device_info),
                device_channel: Some(device_channel),
            }))?;
        } else {
            info!("device not physical {:?}", device_info);
        }
    }
    Ok(())
}

async fn device_removed(
    event_chan: &mpsc::UnboundedSender<Event>,
    topological_path: &std::path::PathBuf,
) -> Result<(), failure::Error> {
    info!("removing device topo path {:?}", topological_path);
    event_chan.unbounded_send(Event::OIR(OIRInfo {
        action: network_manager_core::oir::Action::REMOVE,
        topological_path: topological_path.to_str().unwrap().to_string(),
        file_path: "".to_string(),
        device_information: None,
        device_channel: None,
    }))?;
    Ok(())
}

async fn run(event_chan: mpsc::UnboundedSender<Event>) -> Result<(), failure::Error> {
    let path = ethdir_path();
    let path_as_str = path.to_str().ok_or(format_err!(
        "Path requested for watch is non-utf8 and our
 non-blocking directory apis require utf8 paths: {:?}.",
        path
    ))?;
    info!("device path: {:?}", path);
    let dir_proxy = open_directory_in_namespace(path_as_str, OPEN_RIGHT_READABLE)?;
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(dir_proxy)
        .await
        .with_context(|_| format!("could not watch {:?}", path))?;

    while let Some(fuchsia_vfs_watcher::WatchMessage { event, filename }) =
        watcher.try_next().await.with_context(|_| format!("watching {:?}", path))?
    {
        match event {
            fuchsia_vfs_watcher::WatchEvent::ADD_FILE => {
                info!("Event received {:?} filename: {:?}", event, filename);
                device_found(&event_chan, &filename).await?;
            }

            fuchsia_vfs_watcher::WatchEvent::EXISTING => {
                info!("Event received {:?} filename: {:?}", event, filename);
                device_found(&event_chan, &filename).await?;
            }

            fuchsia_vfs_watcher::WatchEvent::IDLE => {
                info!("Idle Event received {:?} filename: {:?}", event, filename);
            }

            fuchsia_vfs_watcher::WatchEvent::REMOVE_FILE => {
                info!("Remove Event received {:?} filename: {:?}", event, filename);
                device_removed(&event_chan, &filename).await?;
            }

            event => {
                info!("Unknown Event received {:?} filename: {:?}", event, filename);
            }
        }
    }
    Ok(())
}

/// `OirWorker` implements the Online Insertion Removal worker.
/// It monitors the dev file system for added or removed ethernet devices and generates events
/// according to those changes.
pub(crate) struct OirWorker;

impl OirWorker {
    pub fn spawn(self, event_chan: mpsc::UnboundedSender<Event>) {
        info!("Starting oir service");
        fasync::spawn_local(
            run(event_chan).unwrap_or_else(|err: Error| error!("Sending event error {:?}", err)),
        );
    }
}
