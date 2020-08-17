// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error},
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_paver::{
        Asset, BootManagerMarker, BootManagerProxy, Configuration, DataSinkMarker, DataSinkProxy,
        PaverMarker, WriteFirmwareResult,
    },
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_zircon::{Status, VmoChildOptions},
    thiserror::Error,
    update_package::{Image, UpdatePackage},
};

mod configuration;
pub use configuration::InactiveConfiguration;
use configuration::{ActiveConfiguration, TargetConfiguration};

#[derive(Debug, Error)]
enum WriteAssetError {
    #[error("while performing write_asset call")]
    Fidl(#[from] fidl::Error),

    #[error("write_asset responded with")]
    Status(#[from] Status),
}

async fn paver_write_firmware(
    data_sink: &DataSinkProxy,
    configuration: Configuration,
    subtype: &str,
    mut buffer: Buffer,
) -> Result<(), Error> {
    let res = data_sink
        .write_firmware(configuration, subtype, &mut buffer)
        .await
        .context("while performing write_firmware call")?;

    match res {
        WriteFirmwareResult::Status(status) => {
            Status::ok(status).context("firmware failed to write")?;
        }
        WriteFirmwareResult::Unsupported(_) => {
            fx_log_info!("skipping unsupported firmware type: {}", subtype);
        }
    }

    Ok(())
}

async fn paver_write_asset(
    data_sink: &DataSinkProxy,
    configuration: Configuration,
    asset: Asset,
    mut buffer: Buffer,
) -> Result<(), WriteAssetError> {
    let status = data_sink.write_asset(configuration, asset, &mut buffer).await?;
    Status::ok(status)?;
    Ok(())
}

pub async fn paver_read_asset(
    data_sink: &DataSinkProxy,
    configuration: Configuration,
    asset: Asset,
) -> Result<Buffer, Error> {
    let result = data_sink.read_asset(configuration, asset).await?;
    let buffer = result
        .map_err(|status| anyhow!("read_asset responded with {}", Status::from_raw(status)))?;
    Ok(buffer)
}

#[derive(Debug, PartialEq, Eq)]
enum ImageTarget<'a> {
    Firmware { subtype: &'a str, configuration: TargetConfiguration },
    Asset { asset: Asset, configuration: TargetConfiguration },
}

fn classify_image(
    image: &Image,
    inactive_config: InactiveConfiguration,
) -> Result<ImageTarget<'_>, Error> {
    let target = match image.name() {
        "zbi" | "zbi.signed" => ImageTarget::Asset {
            asset: Asset::Kernel,
            configuration: inactive_config.to_target_configuration(),
        },
        "fuchsia.vbmeta" => ImageTarget::Asset {
            asset: Asset::VerifiedBootMetadata,
            configuration: inactive_config.to_target_configuration(),
        },
        "zedboot" | "zedboot.signed" | "recovery" => ImageTarget::Asset {
            asset: Asset::Kernel,
            configuration: TargetConfiguration::Single(Configuration::Recovery),
        },
        "recovery.vbmeta" => ImageTarget::Asset {
            asset: Asset::VerifiedBootMetadata,
            configuration: TargetConfiguration::Single(Configuration::Recovery),
        },
        "bootloader" | "firmware" => {
            // Keep support for update packages still using the older "bootloader" file, which is
            // handled identically to "firmware" but without subtype support.
            ImageTarget::Firmware {
                subtype: image.subtype().unwrap_or(""),
                configuration: inactive_config.to_target_configuration(),
            }
        }
        _ => bail!("unrecognized image: {}", image.name()),
    };

    if let (ImageTarget::Asset { .. }, Some(subtype)) = (&target, image.subtype()) {
        bail!("unsupported subtype {:?} for asset {:?}", subtype, target);
    }

    Ok(target)
}

struct Payload<'a> {
    display_name: &'a str,
    buffer: Buffer,
}

impl Payload<'_> {
    fn clone_buffer(&self) -> Result<Buffer, Status> {
        Ok(Buffer {
            vmo: self.buffer.vmo.create_child(
                VmoChildOptions::COPY_ON_WRITE | VmoChildOptions::RESIZABLE,
                0,
                self.buffer.size,
            )?,
            size: self.buffer.size,
        })
    }
}

/// Writes the given image to the configuration/asset location. If configuration is not given, the
/// image is written to both A and B (if the B partition exists).
async fn write_asset_to_configurations(
    data_sink: &DataSinkProxy,
    configuration: TargetConfiguration,
    asset: Asset,
    payload: Payload<'_>,
) -> Result<(), Error> {
    match configuration {
        TargetConfiguration::Single(configuration) => {
            // Devices supports ABR and/or a specific configuration (ex. Recovery) was requested.
            paver_write_asset(data_sink, configuration, asset, payload.buffer).await?;
        }
        TargetConfiguration::AB => {
            // Device does not support ABR, so write the image to the A partition.
            //
            // Also try to write the image to the B partition to be forwards compatible with devices
            // that will eventually support ABR. If the device does not have a B partition yet, log the
            // error and continue.

            paver_write_asset(data_sink, Configuration::A, asset, payload.clone_buffer()?).await?;

            let res = paver_write_asset(data_sink, Configuration::B, asset, payload.buffer).await;
            if let Err(WriteAssetError::Status(Status::NOT_SUPPORTED)) = res {
                fx_log_warn!("skipping writing {} to B", payload.display_name);
            } else {
                res?;
            }
        }
    }

    Ok(())
}

async fn write_firmware_to_configurations(
    data_sink: &DataSinkProxy,
    configuration: TargetConfiguration,
    subtype: &str,
    payload: Payload<'_>,
) -> Result<(), Error> {
    match configuration {
        TargetConfiguration::Single(configuration) => {
            // Devices supports ABR and/or a specific configuration (ex. Recovery) was requested.
            paver_write_firmware(data_sink, configuration, subtype, payload.buffer).await?;
        }
        TargetConfiguration::AB => {
            // For device that does not support ABR. There will only be one single
            // partition for that firmware. The configuration parameter should be Configuration::A.
            paver_write_firmware(data_sink, Configuration::A, subtype, payload.clone_buffer()?)
                .await?;
            // Similar to asset, we also write Configuration::B to be forwards compatible with
            // devices that will eventually support ABR. For device that does not support A/B, it
            // will log/report WriteFirmwareResult::Unsupported and the paving  will be
            // skipped.
            paver_write_firmware(data_sink, Configuration::B, subtype, payload.buffer).await?;
        }
    }

    Ok(())
}

async fn write_image_buffer(
    data_sink: &DataSinkProxy,
    buffer: Buffer,
    image: &Image,
    inactive_config: InactiveConfiguration,
) -> Result<(), Error> {
    let target = classify_image(image, inactive_config)?;
    let payload = Payload { display_name: image.filename(), buffer };

    match target {
        ImageTarget::Firmware { subtype, configuration } => {
            write_firmware_to_configurations(data_sink, configuration, subtype, payload).await?;
        }
        ImageTarget::Asset { asset, configuration } => {
            write_asset_to_configurations(data_sink, configuration, asset, payload).await?;
        }
    }

    Ok(())
}

pub fn connect_in_namespace() -> Result<(DataSinkProxy, BootManagerProxy), Error> {
    let paver = fuchsia_component::client::connect_to_service::<PaverMarker>()
        .context("connect to fuchsia.paver.Paver")?;

    let (data_sink, server_end) = fidl::endpoints::create_proxy::<DataSinkMarker>()?;
    let () = paver.find_data_sink(server_end).context("connect to fuchsia.paver.DataSink")?;

    let (boot_manager, server_end) = fidl::endpoints::create_proxy::<BootManagerMarker>()?;
    let () = paver.find_boot_manager(server_end).context("connect to fuchsia.paver.BootManager")?;

    Ok((data_sink, boot_manager))
}

/// Determines the active configuration which will be used as the default boot choice on a normal
/// cold boot, which may or may not be the currently running configuration, or none if no
/// configurations are currently bootable.
pub async fn paver_query_active_configuration(
    boot_manager: &BootManagerProxy,
) -> Result<ActiveConfiguration, Error> {
    match boot_manager.query_active_configuration().await {
        Ok(Ok(Configuration::A)) => Ok(ActiveConfiguration::A),
        Ok(Ok(Configuration::B)) => Ok(ActiveConfiguration::B),
        Ok(Ok(Configuration::Recovery)) => {
            fx_log_err!("ignoring active configuration of recovery and assuming A is active");
            Ok(ActiveConfiguration::A)
        }
        // FIXME Configuration::Recovery isn't actually possible.
        //Ok(Ok(configuration)) => Err(anyhow!(
        //    "query_active_configuration responded with invalid configuration: {:?}",
        //    configuration
        //)),
        Ok(Err(status)) => {
            Err(anyhow!("query_active_configuration responded with {}", Status::from_raw(status)))
        }
        Err(fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. }) => {
            fx_log_warn!("device does not support ABR. Kernel image updates will not be atomic.");
            Ok(ActiveConfiguration::NotSupported)
        }
        Err(err) => Err(anyhow!(err).context("while performing query_active_configuration call")),
    }
}

/// Determines the current inactive configuration to which new kernel images should be written.
pub async fn query_inactive_configuration(
    boot_manager: &BootManagerProxy,
) -> Result<InactiveConfiguration, Error> {
    let active_config = paver_query_active_configuration(boot_manager).await?;

    Ok(active_config.to_inactive_configuration())
}

/// Sets the given inactive `configuration` as active for subsequent boot attempts. If ABR is not
/// supported, do nothing.
pub async fn set_configuration_active(
    boot_manager: &BootManagerProxy,
    configuration: InactiveConfiguration,
) -> Result<(), Error> {
    if let Some(configuration) = configuration.to_configuration() {
        let status = boot_manager
            .set_configuration_active(configuration)
            .await
            .context("while performing set_configuration_active call")?;
        Status::ok(status).context("set_configuration_active responded with")?;
    }
    Ok(())
}

async fn set_configuration_unbootable(
    boot_manager: &BootManagerProxy,
    configuration: Configuration,
) -> Result<(), Error> {
    let status = boot_manager
        .set_configuration_unbootable(configuration)
        .await
        .context("while performing set_configuration_unbootable call")?;
    Status::ok(status).context("set_configuration_unbootable responded with")?;
    Ok(())
}

/// Sets the recovery configuration as the only valid boot option by marking the A and B
/// configurations unbootable.
pub async fn set_recovery_configuration_active(
    boot_manager: &BootManagerProxy,
) -> Result<(), Error> {
    let () = set_configuration_unbootable(boot_manager, Configuration::A)
        .await
        .context("while marking Configuration::A unbootable")?;
    let () = set_configuration_unbootable(boot_manager, Configuration::B)
        .await
        .context("while marking Configuration::B unbootable")?;
    Ok(())
}

/// Write the given `image` from the update package through the paver to the appropriate
/// partitions.
pub async fn write_image(
    data_sink: &DataSinkProxy,
    update_pkg: &UpdatePackage,
    image: &Image,
    inactive_config: InactiveConfiguration,
) -> Result<(), Error> {
    let buffer = update_pkg
        .open_image(&image)
        .await
        .with_context(|| format!("error opening {}", image.filename()))?;

    fx_log_info!("writing {} from update package", image.filename());

    let res = write_image_buffer(data_sink, buffer, image, inactive_config)
        .await
        .with_context(|| format!("error writing {}", image.filename()));

    if let Ok(()) = &res {
        fx_log_info!("wrote {} successfully", image.filename());
    }
    res
}

/// Flush pending disk writes and boot configuration, if supported.
pub async fn flush(
    data_sink: &DataSinkProxy,
    boot_manager: &BootManagerProxy,
    inactive_config: InactiveConfiguration,
) -> Result<(), Error> {
    let () = Status::ok(
        data_sink.flush().await.context("while performing fuchsia.paver.DataSink/Flush call")?,
    )
    .context("fuchsia.paver.DataSink/Flush responded with")?;

    match inactive_config {
        InactiveConfiguration::A | InactiveConfiguration::B => {
            let () = Status::ok(
                boot_manager
                    .flush()
                    .await
                    .context("while performing fuchsia.paver.BootManager/Flush call")?,
            )
            .context("fuchsia.paver.BootManager/Flush responded with")?;
        }
        InactiveConfiguration::NotSupported => {}
    }

    Ok(())
}

#[cfg(test)]
fn make_buffer(contents: impl AsRef<[u8]>) -> Buffer {
    use {
        fuchsia_zircon::{Vmo, VmoOptions},
        std::convert::TryInto,
    };
    let contents = contents.as_ref();
    let size = contents.len().try_into().unwrap();

    let vmo = Vmo::create_with_opts(VmoOptions::RESIZABLE, size).unwrap();
    vmo.write(contents, 0).unwrap();

    Buffer { vmo, size }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_paver::DataSinkMarker,
        matches::assert_matches,
        mock_paver::{MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_inactive_configuration_with_a_active() {
        let paver =
            Arc::new(MockPaverServiceBuilder::new().active_config(Configuration::A).build());
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_inactive_configuration(&boot_manager).await.unwrap(),
            InactiveConfiguration::B
        );

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryActiveConfiguration]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_inactive_configuration_with_b_active() {
        let paver =
            Arc::new(MockPaverServiceBuilder::new().active_config(Configuration::B).build());
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_inactive_configuration(&boot_manager).await.unwrap(),
            InactiveConfiguration::A
        );

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryActiveConfiguration]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_inactive_configuration_with_invalid_active() {
        let paver =
            Arc::new(MockPaverServiceBuilder::new().active_config(Configuration::Recovery).build());
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_inactive_configuration(&boot_manager).await.unwrap(),
            InactiveConfiguration::B
        );

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryActiveConfiguration]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_configuration_active_makes_call() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        set_configuration_active(&boot_manager, InactiveConfiguration::B).await.unwrap();

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::SetConfigurationActive { configuration: Configuration::B }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_recovery_configuration_active_makes_calls() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        set_recovery_configuration_active(&boot_manager).await.unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::SetConfigurationUnbootable { configuration: Configuration::A },
                PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_firmware() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware contents"),
            &Image::new("bootloader"),
            InactiveConfiguration::A,
        )
        .await
        .unwrap();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware_foo contents"),
            &Image::join("firmware", "foo"),
            InactiveConfiguration::B,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteFirmware {
                    configuration: Configuration::A,
                    firmware_type: "".to_owned(),
                    payload: b"firmware contents".to_vec()
                },
                PaverEvent::WriteFirmware {
                    configuration: Configuration::B,
                    firmware_type: "foo".to_owned(),
                    payload: b"firmware_foo contents".to_vec()
                }
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_ignores_unsupported_firmware() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .firmware_hook(|event| match event {
                    PaverEvent::WriteFirmware { .. } => WriteFirmwareResult::Unsupported(true),
                    _ => panic!("Unexpected event: {:?}", event),
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware of the future!"),
            &Image::join("firmware", "unknown"),
            InactiveConfiguration::A,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteFirmware {
                configuration: Configuration::A,
                firmware_type: "unknown".to_owned(),
                payload: b"firmware of the future!".to_vec()
            },]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_forwards_other_firmware_errors() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .firmware_hook(|event| match event {
                    PaverEvent::WriteFirmware { .. } => {
                        WriteFirmwareResult::Status(Status::INTERNAL.into_raw())
                    }
                    _ => panic!("Unexpected event: {:?}", event),
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("oops"),
                &Image::join("firmware", "error"),
                InactiveConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("firmware failed to write")
        );

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteFirmware {
                configuration: Configuration::A,
                firmware_type: "error".to_owned(),
                payload: b"oops".to_vec()
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_rejects_unknown_images() {
        let (data_sink, _server_end) = fidl::endpoints::create_proxy::<DataSinkMarker>().unwrap();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("unknown"),
                &Image::new("unknown"),
                InactiveConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("unrecognized image")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_rejects_asset_with_subtype() {
        let (data_sink, _server_end) = fidl::endpoints::create_proxy::<DataSinkMarker>().unwrap();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("unknown"),
                &Image::join("zbi", "2"),
                InactiveConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("unsupported subtype")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_asset_to_single_config() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi contents"),
            &Image::new("zbi"),
            InactiveConfiguration::A,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteAsset {
                configuration: Configuration::A,
                asset: Asset::Kernel,
                payload: b"zbi contents".to_vec()
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_write_asset_forwards_errors() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .call_hook(|event| match event {
                    PaverEvent::WriteAsset { .. } => Status::INTERNAL,
                    _ => panic!("Unexpected event: {:?}", event),
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi contents"),
                &Image::new("zbi"),
                InactiveConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("vbmeta contents"),
                &Image::new("fuchsia.vbmeta"),
                InactiveConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi contents"),
                &Image::new("zbi"),
                InactiveConfiguration::B,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("vbmeta contents"),
                &Image::new("fuchsia.vbmeta"),
                InactiveConfiguration::B,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_supports_known_image_names() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        for name in &[
            "bootloader",
            "firmware",
            "zbi",
            "zbi.signed",
            "fuchsia.vbmeta",
            "zedboot",
            "zedboot.signed",
            "recovery",
            "recovery.vbmeta",
        ] {
            write_image_buffer(
                &data_sink,
                make_buffer(format!("{} buffer", name)),
                &Image::new(*name),
                InactiveConfiguration::B,
            )
            .await
            .unwrap();
        }

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteFirmware {
                    configuration: Configuration::B,
                    firmware_type: "".to_owned(),
                    payload: b"bootloader buffer".to_vec()
                },
                PaverEvent::WriteFirmware {
                    configuration: Configuration::B,
                    firmware_type: "".to_owned(),
                    payload: b"firmware buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::VerifiedBootMetadata,
                    payload: b"fuchsia.vbmeta buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::Recovery,
                    asset: Asset::Kernel,
                    payload: b"zedboot buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::Recovery,
                    asset: Asset::Kernel,
                    payload: b"zedboot.signed buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::Recovery,
                    asset: Asset::Kernel,
                    payload: b"recovery buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::Recovery,
                    asset: Asset::VerifiedBootMetadata,
                    payload: b"recovery.vbmeta buffer".to_vec()
                },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn flush_makes_calls() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();
        let boot_manager = paver.spawn_boot_manager_service();

        flush(&data_sink, &boot_manager, InactiveConfiguration::A).await.unwrap();
        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::DataSinkFlush, PaverEvent::BootManagerFlush,]
        );

        flush(&data_sink, &boot_manager, InactiveConfiguration::B).await.unwrap();
        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::DataSinkFlush, PaverEvent::BootManagerFlush,]
        );
    }
}

#[cfg(test)]
mod abr_not_supported_tests {
    use {
        super::*,
        matches::assert_matches,
        mock_paver::{MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_inactive_configuration_returns_not_supported() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_inactive_configuration(&boot_manager).await.unwrap(),
            InactiveConfiguration::NotSupported
        );

        assert_eq!(paver.take_events(), vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_configuration_active_is_noop() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        set_configuration_active(&boot_manager, InactiveConfiguration::NotSupported).await.unwrap();

        assert_eq!(paver.take_events(), vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_asset_to_both_configs() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi.signed contents"),
            &Image::new("zbi.signed"),
            InactiveConfiguration::NotSupported,
        )
        .await
        .unwrap();

        write_image_buffer(
            &data_sink,
            make_buffer("the new vbmeta"),
            &Image::new("fuchsia.vbmeta"),
            InactiveConfiguration::NotSupported,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::VerifiedBootMetadata,
                    payload: b"the new vbmeta".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::VerifiedBootMetadata,
                    payload: b"the new vbmeta".to_vec()
                },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_firmware_to_both_configs() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware contents"),
            &Image::new("firmware"),
            InactiveConfiguration::NotSupported,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteFirmware {
                    configuration: Configuration::A,
                    firmware_type: "".to_owned(),
                    payload: b"firmware contents".to_vec()
                },
                PaverEvent::WriteFirmware {
                    configuration: Configuration::B,
                    firmware_type: "".to_owned(),
                    payload: b"firmware contents".to_vec()
                },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_forwards_config_a_error() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .call_hook(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::A,
                        asset: Asset::Kernel,
                        payload: _,
                    } => Status::INTERNAL,
                    _ => Status::OK,
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi.signed contents"),
                &Image::new("zbi.signed"),
                InactiveConfiguration::NotSupported,
            )
            .await,
            Err(_)
        );

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteAsset {
                configuration: Configuration::A,
                asset: Asset::Kernel,
                payload: b"zbi.signed contents".to_vec()
            },]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_ignores_config_b_not_supported_error() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .call_hook(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::B,
                        asset: Asset::Kernel,
                        payload: _,
                    } => Status::NOT_SUPPORTED,
                    _ => Status::OK,
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi.signed contents"),
            &Image::new("zbi.signed"),
            InactiveConfiguration::NotSupported,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_write_firmware_ignores_unsupported_config_b() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .firmware_hook(|event| match event {
                    PaverEvent::WriteFirmware {
                        configuration: Configuration::B,
                        firmware_type: _,
                        payload: _,
                    } => WriteFirmwareResult::Unsupported(true),
                    _ => WriteFirmwareResult::Status(Status::OK.into_raw()),
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware contents"),
            &Image::new("firmware"),
            InactiveConfiguration::NotSupported,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteFirmware {
                    configuration: Configuration::A,
                    firmware_type: "".to_owned(),
                    payload: b"firmware contents".to_vec()
                },
                PaverEvent::WriteFirmware {
                    configuration: Configuration::B,
                    firmware_type: "".to_owned(),
                    payload: b"firmware contents".to_vec()
                },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_forwards_other_config_b_errors() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .call_hook(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::B,
                        asset: Asset::Kernel,
                        ..
                    } => Status::INTERNAL,
                    _ => Status::OK,
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi.signed contents"),
                &Image::new("zbi.signed"),
                InactiveConfiguration::NotSupported,
            )
            .await,
            Err(_)
        );

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn flush_makes_calls() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();
        let boot_manager = paver.spawn_boot_manager_service();

        flush(&data_sink, &boot_manager, InactiveConfiguration::NotSupported).await.unwrap();
        assert_eq!(paver.take_events(), vec![PaverEvent::DataSinkFlush,]);
    }
}
