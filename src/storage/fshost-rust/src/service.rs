// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{crypt::fxfs, device::constants, volume::resize_volume, watcher},
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::{Proxy, RequestStream},
    fidl_fuchsia_fshost as fshost,
    fidl_fuchsia_hardware_block::BlockMarker,
    fidl_fuchsia_hardware_block_volume::VolumeAndNodeMarker,
    fidl_fuchsia_io::OpenFlags,
    fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream},
    fs_management::{
        filesystem::{ServingMultiVolumeFilesystem, ServingSingleVolumeFilesystem},
        format::{detect_disk_format, DiskFormat},
        partition::{open_partition, PartitionMatcher},
        Fxfs, Minfs,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_fs::{
        directory::{create_directory_recursive, open_file},
        file::write,
    },
    fuchsia_runtime::HandleType,
    fuchsia_zircon::{self as zx, Duration},
    futures::{channel::mpsc, StreamExt, TryStreamExt},
    std::sync::Arc,
    vfs::service,
};

pub enum FshostShutdownResponder {
    Lifecycle(LifecycleRequestStream),
}

impl FshostShutdownResponder {
    pub fn close(self) -> Result<(), fidl::Error> {
        match self {
            FshostShutdownResponder::Lifecycle(_) => {}
        }
        Ok(())
    }
}

const OPEN_PARTITION_DURATION: Duration = Duration::from_seconds(10);
const DATA_PARTITION_LABEL: &str = "data";
const LEGACY_DATA_PARTITION_LABEL: &str = "minfs";

enum Filesystem {
    Serving(ServingSingleVolumeFilesystem),
    ServingMultiVolume(ServingMultiVolumeFilesystem),
}

impl Filesystem {
    fn root(&mut self) -> Result<&fidl_fuchsia_io::DirectoryProxy, Error> {
        match self {
            Filesystem::Serving(fs) => Ok(fs.root()),
            Filesystem::ServingMultiVolume(fs) => {
                Ok(fs.volume("data").ok_or(anyhow!("no data volume"))?.root())
            }
        }
    }
}

async fn write_data_file(
    config: &Arc<fshost_config::Config>,
    filename: &str,
    payload: zx::Vmo,
) -> Result<(), Error> {
    if !config.fvm_ramdisk && !config.netboot {
        return Err(anyhow!(
            "Can't WriteDataFile from a non-recovery build;
            fvm_ramdisk must be set."
        ));
    }

    let content_size = if let Ok(content_size) = payload.get_content_size() {
        content_size
    } else if let Ok(content_size) = payload.get_size() {
        content_size
    } else {
        return Err(anyhow!("Failed to get content size"));
    };

    let content_size =
        usize::try_from(content_size).context("Failed to convert u64 content_size to usize")?;

    assert_eq!(config.ramdisk_prefix.is_empty(), false);

    let fvm_matcher = PartitionMatcher {
        detected_disk_format: Some(DiskFormat::Fvm),
        ignore_prefix: Some(config.ramdisk_prefix.clone()),
        ..Default::default()
    };

    let fvm_path =
        open_partition(fvm_matcher, OPEN_PARTITION_DURATION).await.context("Failed to find FVM")?;
    let format =
        if config.data_filesystem_format == "fxfs" { DiskFormat::Fxfs } else { DiskFormat::Minfs };
    let data_partition_names =
        vec![DATA_PARTITION_LABEL.to_string(), LEGACY_DATA_PARTITION_LABEL.to_string()];

    let data_matcher = PartitionMatcher {
        type_guid: Some(constants::DATA_TYPE_GUID),
        labels: Some(data_partition_names.clone()),
        parent_device: Some(fvm_path),
        ignore_if_path_contains: Some("zxcrypt/unsealed".to_string()),
        ..Default::default()
    };

    let mut partition_path = open_partition(data_matcher, OPEN_PARTITION_DURATION)
        .await
        .context("Failed to open partition")?;

    let mut inside_zxcrypt = false;
    if format != DiskFormat::Fxfs && !config.no_zxcrypt {
        let mut zxcrypt_path = partition_path;
        zxcrypt_path.push_str("/zxcrypt/unsealed");
        let zxcrypt_matcher = PartitionMatcher {
            type_guid: Some(constants::DATA_TYPE_GUID),
            labels: Some(data_partition_names),
            parent_device: Some(zxcrypt_path),
            ..Default::default()
        };
        partition_path = open_partition(zxcrypt_matcher, OPEN_PARTITION_DURATION)
            .await
            .context("Failed to open zxcrypt partition")?;
        inside_zxcrypt = true;
    }
    let partition_proxy = connect_to_protocol_at_path::<BlockMarker>(&partition_path)?;
    let detected_format = detect_disk_format(&partition_proxy).await;
    tracing::info!(
        "Using data partition at {:?}, has format {:?}",
        partition_path,
        detected_format
    );
    let volume_proxy = connect_to_protocol_at_path::<VolumeAndNodeMarker>(&partition_path)?;
    let mut serving_fs = match format {
        DiskFormat::Fxfs => {
            let mut different_format = false;
            if detected_format != format {
                tracing::info!("Data partition is not in expected format; reformatting");
                let target_size = config.data_max_bytes;
                tracing::info!("Resizing data volume, target = {:?} bytes", target_size);
                let actual_size = resize_volume(&volume_proxy, target_size, inside_zxcrypt).await?;
                if actual_size < target_size {
                    tracing::info!("Only allocated {:?} bytes", actual_size);
                }
                different_format = true;
            }
            let mut fxfs = Fxfs::from_channel(volume_proxy.into_channel().unwrap().into())?;
            if different_format {
                fxfs.format().await.context("Failed to format Fxfs")?;
            }
            let mut serving_fxfs =
                fxfs.serve_multi_volume().await.context("Failed to serve_multi_volume")?;
            if different_format {
                fxfs::init_data_volume(&mut serving_fxfs, config)
                    .await
                    .context("Failed to initialize the data volume")?
            } else {
                fxfs::unlock_data_volume(&mut serving_fxfs, config)
                    .await
                    .context("Failed to unlock the data volume")?
            };
            Filesystem::ServingMultiVolume(serving_fxfs)
        }
        DiskFormat::Minfs => {
            let mut minfs = Minfs::from_channel(volume_proxy.into_channel().unwrap().into())
                .context("Failed to create minfs")?;
            if detected_format != format {
                minfs.format().await.context("Failed to format minfs")?;
            }
            let serving_minfs = minfs.serve().await.context("Failed to serve minfs")?;
            Filesystem::Serving(serving_minfs)
        }
        _ => unreachable!(),
    };
    let data_root = serving_fs.root().context("Failed to get data root")?;
    let (directory_path, relative_file_path) =
        filename.rsplit_once("/").ok_or(anyhow!("There is no backslash in the file path"))?;
    let directory_proxy = create_directory_recursive(
        data_root,
        directory_path,
        OpenFlags::CREATE | OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
    )
    .await
    .context("Failed to create directory")?;
    let file_proxy = open_file(
        &directory_proxy,
        relative_file_path,
        OpenFlags::CREATE | OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
    )
    .await
    .context("Failed to open file")?;
    let mut data = vec![0; content_size];
    payload.read(&mut data, 0)?;
    write(&file_proxy, &data).await?;
    if let Filesystem::Serving(filesystem) = serving_fs {
        filesystem.shutdown().await.context("Failed to shutdown minfs")?;
    }
    return Ok(());
}

/// Make a new vfs service node that implements fuchsia.fshost.Admin
pub fn fshost_admin(config: &Arc<fshost_config::Config>) -> Arc<service::Service> {
    let config = config.clone();
    service::host(move |mut stream: fshost::AdminRequestStream| {
        let config = config.clone();
        async move {
            while let Some(request) = stream.next().await {
                match request {
                    Ok(fshost::AdminRequest::Mount { responder, .. }) => {
                        responder
                            .send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                            .unwrap_or_else(|e| {
                                tracing::error!("failed to send Mount response. error: {:?}", e);
                            });
                    }
                    Ok(fshost::AdminRequest::Unmount { responder, .. }) => {
                        responder
                            .send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                            .unwrap_or_else(|e| {
                                tracing::error!("failed to send Unmount response. error: {:?}", e);
                            });
                    }
                    Ok(fshost::AdminRequest::GetDevicePath { responder, .. }) => {
                        responder
                            .send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                            .unwrap_or_else(|e| {
                                tracing::error!(
                                    "failed to send GetDevicePath response. error: {:?}",
                                    e
                                );
                            });
                    }
                    Ok(fshost::AdminRequest::WriteDataFile { responder, payload, filename }) => {
                        let mut res = match write_data_file(&config, &filename, payload).await {
                            Ok(()) => Ok(()),
                            Err(e) => {
                                tracing::error!("admin service: write_data_file failed: {:?}", e);
                                Err(zx::Status::INTERNAL.into_raw())
                            }
                        };
                        responder.send(&mut res).unwrap_or_else(|e| {
                            tracing::error!(
                                "failed to send WriteDataFile response. error: {:?}",
                                e
                            );
                        });
                    }
                    Ok(fshost::AdminRequest::WipeStorage { responder, .. }) => {
                        responder
                            .send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                            .unwrap_or_else(|e| {
                                tracing::error!(
                                    "failed to send WipeStorage response. error: {:?}",
                                    e
                                );
                            });
                    }
                    Err(e) => {
                        tracing::error!("admin server failed: {:?}", e);
                        return;
                    }
                }
            }
        }
    })
}

/// Create a new service node which implements the fuchsia.fshost.BlockWatcher protocol.
pub fn fshost_block_watcher(pauser: watcher::Watcher) -> Arc<service::Service> {
    service::host(move |mut stream: fshost::BlockWatcherRequestStream| {
        let mut pauser = pauser.clone();
        async move {
            while let Some(request) = stream.next().await {
                match request {
                    Ok(fshost::BlockWatcherRequest::Pause { responder }) => {
                        let res = match pauser.pause().await {
                            Ok(()) => zx::Status::OK.into_raw(),
                            Err(e) => {
                                tracing::error!("block watcher service: failed to pause: {:?}", e);
                                zx::Status::UNAVAILABLE.into_raw()
                            }
                        };
                        responder.send(res).unwrap_or_else(|e| {
                            tracing::error!("failed to send Pause response. error: {:?}", e);
                        });
                    }
                    Ok(fshost::BlockWatcherRequest::Resume { responder }) => {
                        let res = match pauser.resume().await {
                            Ok(()) => zx::Status::OK.into_raw(),
                            Err(e) => {
                                tracing::error!("block watcher service: failed to resume: {:?}", e);
                                zx::Status::BAD_STATE.into_raw()
                            }
                        };
                        responder.send(res).unwrap_or_else(|e| {
                            tracing::error!("failed to send Resume response. error: {:?}", e);
                        });
                    }
                    Err(e) => {
                        tracing::error!("block watcher server failed: {:?}", e);
                        return;
                    }
                }
            }
        }
    })
}

pub fn handle_lifecycle_requests(
    mut shutdown: mpsc::Sender<FshostShutdownResponder>,
) -> Result<(), Error> {
    if let Some(handle) = fuchsia_runtime::take_startup_handle(HandleType::Lifecycle.into()) {
        let mut stream =
            LifecycleRequestStream::from_channel(fasync::Channel::from_channel(handle.into())?);
        fasync::Task::spawn(async move {
            if let Ok(Some(LifecycleRequest::Stop { .. })) = stream.try_next().await {
                shutdown.start_send(FshostShutdownResponder::Lifecycle(stream)).unwrap_or_else(
                    |e| tracing::error!("failed to send shutdown message. error: {:?}", e),
                );
            }
        })
        .detach();
    }
    Ok(())
}
