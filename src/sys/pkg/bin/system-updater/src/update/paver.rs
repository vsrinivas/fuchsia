// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error},
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_paver::{
        Asset, BootManagerMarker, BootManagerProxy, Configuration, ConfigurationStatus,
        DataSinkMarker, DataSinkProxy, PaverMarker, WriteFirmwareResult,
    },
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_zircon::{Status, VmoChildOptions},
    thiserror::Error,
    update_package::{Image, UpdatePackage},
};

mod configuration;
use configuration::{ActiveConfiguration, TargetConfiguration};
pub use configuration::{CurrentConfiguration, NonCurrentConfiguration};

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
    desired_config: NonCurrentConfiguration,
) -> Result<ImageTarget<'_>, Error> {
    let target = match image.name() {
        "zbi" | "zbi.signed" => ImageTarget::Asset {
            asset: Asset::Kernel,
            configuration: desired_config.to_target_configuration(),
        },
        "fuchsia.vbmeta" => ImageTarget::Asset {
            asset: Asset::VerifiedBootMetadata,
            configuration: desired_config.to_target_configuration(),
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
                configuration: desired_config.to_target_configuration(),
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
    desired_config: NonCurrentConfiguration,
) -> Result<(), Error> {
    let target = classify_image(image, desired_config)?;
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
pub async fn query_active_configuration(
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

/// Retrieve the currently-running configuration from the paver service (the configuration the
/// device booted from) which may be distinct from the 'active' configuration.
pub async fn query_current_configuration(
    boot_manager: &BootManagerProxy,
) -> Result<CurrentConfiguration, Error> {
    match boot_manager.query_current_configuration().await {
        Ok(Ok(Configuration::A)) => Ok(CurrentConfiguration::A),
        Ok(Ok(Configuration::B)) => Ok(CurrentConfiguration::B),
        Ok(Ok(Configuration::Recovery)) => Ok(CurrentConfiguration::Recovery),
        Ok(Err(status)) => {
            Err(anyhow!("query_current_configuration responded with {}", Status::from_raw(status)))
        }
        Err(fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. }) => {
            fx_log_warn!("device does not support ABR. Kernel image updates will not be atomic.");
            Ok(CurrentConfiguration::NotSupported)
        }
        Err(err) => Err(anyhow!(err).context("while performing query_current_configuration call")),
    }
}

async fn paver_query_configuration_status(
    boot_manager: &BootManagerProxy,
    configuration: Configuration,
) -> Result<ConfigurationStatus, Error> {
    match boot_manager.query_configuration_status(configuration.into()).await {
        Ok(Ok(configuration_status)) => Ok(configuration_status),
        Ok(Err(status)) => {
            Err(anyhow!("query_configuration_status responded with {}", Status::from_raw(status)))
        }
        Err(err) => Err(anyhow!(err).context("while performing query_configuration_status call")),
    }
}

async fn paver_set_active_configuration_healthy(
    boot_manager: &BootManagerProxy,
) -> Result<(), Error> {
    let status = boot_manager
        .set_active_configuration_healthy()
        .await
        .context("while performing set_active_configuration_healthy call")?;
    Status::ok(status).context("set_active_configuration_healthy responded with")?;
    Ok(())
}

/// If the current partition is either A or B, ensure that it is also the active partition.
///
/// This operation can fail, and it is very important that all states it can end in (by returning)
/// don't end in a bricked device when the update continues.
/// Some devices use BootManager's flush to flush operations, and some don't. This function needs
/// to support both strategies.
pub async fn ensure_current_partition_active(
    boot_manager: &BootManagerProxy,
    current: CurrentConfiguration,
) -> Result<(), Error> {
    if current == CurrentConfiguration::NotSupported {
        fx_log_info!("ABR not supported, not setting current configuration to active");
        return Ok(());
    }

    if !current.is_primary_configuration() {
        fx_log_info!(
            "Current partition is neither A nor B, not resetting the active partition to it"
        );
        return Ok(());
    }

    let originally_active = query_active_configuration(boot_manager)
        .await
        .context("while determining originally active configuration")?;

    if current.same_primary_configuration_as_active(originally_active) {
        fx_log_info!(
            "Current partition ({:?}) is already active, not resetting active ({:?})",
            current,
            originally_active
        );

        return Ok(());
    }

    // Internal function because this is dangerous enough that we never want another function
    // to call it - it arbitrarily resets active to current.
    async fn ensure_current_partition_active_without_flush(
        boot_manager: &BootManagerProxy,
        current: CurrentConfiguration,
        originally_active: ActiveConfiguration,
    ) -> Result<(), Error> {
        {
            let current_configuration = if let Some(configuration) = current.to_configuration() {
                configuration
            } else {
                return Ok(());
            };

            let original_status_of_current_partition =
                paver_query_configuration_status(boot_manager, current_configuration).await?;

            // This will reset the boot counter on the current partition, which could be a
            // problem if it is PENDING, but our health check framework should prevent the
            // system-updater from getting here on a non-healthy current partition
            // If this operation fails and we bail out here, we'll have written nothing.
            let () = paver_set_arbitrary_configuration_active(boot_manager, current_configuration)
                .await
                .context("while setting current partition active")?;

            if original_status_of_current_partition == ConfigurationStatus::Healthy {
                // If this operation fails and we bail out of the entire update, we'll have set the
                // current configuration active, but not healthy.
                // That will result in the next boot going into that partition and running the
                // health check, which will set its health status appropriately.
                let () = paver_set_active_configuration_healthy(boot_manager)
                    .await
                    .context("while setting current partition healthy")?;
            }
        }

        {
            let originally_active_configuration = if let Some(configuration) =
                originally_active.to_configuration()
            {
                configuration
            } else {
                fx_log_info!("active partition is not transformable to a Configuration, not setting originally active unbootable");
                return Ok(());
            };

            let () = set_configuration_unbootable(boot_manager, originally_active_configuration)
                .await
                .context("while setting originally active configuration unbootable")?;
        }

        Ok(())
    }

    // We need to flush the boot manager regardless, but if that operation succeeded while something
    // in ensure_current_partition_without_flush failed, propagate that error.
    let result =
        ensure_current_partition_active_without_flush(boot_manager, current, originally_active)
            .await;
    let () = paver_flush_boot_manager(boot_manager).await.context("while flushing boot manager")?;

    result
}

/// Sets an arbitrary configuration active. Not pub because it's possible to use this function to set a
/// current partition or recovery partition active, which is almost certainly not what external users want.
async fn paver_set_arbitrary_configuration_active(
    boot_manager: &BootManagerProxy,
    configuration: Configuration,
) -> Result<(), Error> {
    let status = boot_manager
        .set_configuration_active(configuration)
        .await
        .context("while performing set_configuration_active call")?;
    Status::ok(status).context("set_configuration_active responded with")?;
    Ok(())
}

/// Sets the given desired `configuration` as active for subsequent boot attempts. If ABR is not
/// supported, do nothing.
pub async fn set_configuration_active(
    boot_manager: &BootManagerProxy,
    desired_configuration: NonCurrentConfiguration,
) -> Result<(), Error> {
    if let Some(configuration) = desired_configuration.to_configuration() {
        return paver_set_arbitrary_configuration_active(boot_manager, configuration).await;
    }
    Ok(())
}

/// Set an arbitrary configuration as unbootable. Dangerous!
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
    desired_config: NonCurrentConfiguration,
) -> Result<(), Error> {
    let buffer = update_pkg
        .open_image(&image)
        .await
        .with_context(|| format!("error opening {}", image.filename()))?;

    fx_log_info!("writing {} from update package", image.filename());

    let res = write_image_buffer(data_sink, buffer, image, desired_config)
        .await
        .with_context(|| format!("error writing {}", image.filename()));

    if let Ok(()) = &res {
        fx_log_info!("wrote {} successfully", image.filename());
    }
    res
}

async fn paver_flush_boot_manager(boot_manager: &BootManagerProxy) -> Result<(), Error> {
    let () = Status::ok(
        boot_manager
            .flush()
            .await
            .context("while performing fuchsia.paver.BootManager/Flush call")?,
    )
    .context("fuchsia.paver.BootManager/Flush responded with")?;
    Ok(())
}

/// Flush pending disk writes and boot configuration, if supported.
pub async fn flush(
    data_sink: &DataSinkProxy,
    boot_manager: &BootManagerProxy,
    desired: NonCurrentConfiguration,
) -> Result<(), Error> {
    let () = Status::ok(
        data_sink.flush().await.context("while performing fuchsia.paver.DataSink/Flush call")?,
    )
    .context("fuchsia.paver.DataSink/Flush responded with")?;

    match desired {
        NonCurrentConfiguration::A | NonCurrentConfiguration::B => {
            paver_flush_boot_manager(boot_manager).await.context("while flushing boot manager")?;
        }
        NonCurrentConfiguration::NotSupported => {}
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
    async fn query_non_current_configuration_with_a_current() {
        let paver =
            Arc::new(MockPaverServiceBuilder::new().current_config(Configuration::A).build());
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_current_configuration(&boot_manager)
                .await
                .unwrap()
                .to_non_current_configuration(),
            NonCurrentConfiguration::B,
        );

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_non_current_configuration_with_b_current() {
        let paver =
            Arc::new(MockPaverServiceBuilder::new().current_config(Configuration::B).build());
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_current_configuration(&boot_manager)
                .await
                .unwrap()
                .to_non_current_configuration(),
            NonCurrentConfiguration::A,
        );

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_non_current_configuration_with_r_current() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new().current_config(Configuration::Recovery).build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_current_configuration(&boot_manager)
                .await
                .unwrap()
                .to_non_current_configuration(),
            NonCurrentConfiguration::A, // We default to A
        );

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_configuration_active_makes_call() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        set_configuration_active(&boot_manager, NonCurrentConfiguration::B).await.unwrap();

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
    async fn set_arbitrary_configuration_active_makes_call() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        paver_set_arbitrary_configuration_active(&boot_manager, Configuration::A).await.unwrap();

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::SetConfigurationActive { configuration: Configuration::A }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_configuration_status_makes_call() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        paver_query_configuration_status(&boot_manager, Configuration::B).await.unwrap();

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::QueryConfigurationStatus { configuration: Configuration::B }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_active_configuration_healthy_makes_call() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        paver_set_active_configuration_healthy(&boot_manager).await.unwrap();

        assert_eq!(paver.take_events(), vec![PaverEvent::SetActiveConfigurationHealthy]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn flush_boot_manager_makes_call() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        paver_flush_boot_manager(&boot_manager).await.unwrap();

        assert_eq!(paver.take_events(), vec![PaverEvent::BootManagerFlush]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ensure_current_partition_active_bails_out_if_current_equals_active_with_current_a() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(Configuration::A)
                .active_config(Configuration::A)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        ensure_current_partition_active(&boot_manager, CurrentConfiguration::A).await.unwrap();

        // We should only get as far as finding out the active config.
        assert_eq!(paver.take_events(), vec![PaverEvent::QueryActiveConfiguration,]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ensure_current_partition_active_bails_out_if_current_equals_active_with_current_b() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(Configuration::B)
                .active_config(Configuration::B)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        ensure_current_partition_active(&boot_manager, CurrentConfiguration::B).await.unwrap();

        // We should only get as far as finding out the active config.
        assert_eq!(paver.take_events(), vec![PaverEvent::QueryActiveConfiguration,]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ensure_current_partition_active_bails_out_with_current_r() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(Configuration::Recovery)
                .active_config(Configuration::A)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        ensure_current_partition_active(&boot_manager, CurrentConfiguration::Recovery)
            .await
            .unwrap();

        assert_eq!(paver.take_events(), vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ensure_current_partition_resets_active() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(Configuration::A)
                .active_config(Configuration::B)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        ensure_current_partition_active(&boot_manager, CurrentConfiguration::A).await.unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryActiveConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
                PaverEvent::SetConfigurationActive { configuration: Configuration::A },
                PaverEvent::SetActiveConfigurationHealthy,
                PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
                PaverEvent::BootManagerFlush,
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ensure_current_partition_resets_active_when_current_isnt_healthy() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(Configuration::A)
                .active_config(Configuration::B)
                .config_status_hook(move |event| {
                    if let PaverEvent::QueryConfigurationStatus { configuration } = event {
                        if *configuration == Configuration::A {
                            // The current config is unbootable, all others should be fine.
                            return fidl_fuchsia_paver::ConfigurationStatus::Unbootable;
                        }
                    }
                    fidl_fuchsia_paver::ConfigurationStatus::Healthy
                })
                .build(),
        );

        let boot_manager = paver.spawn_boot_manager_service();

        ensure_current_partition_active(&boot_manager, CurrentConfiguration::A).await.unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryActiveConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
                PaverEvent::SetConfigurationActive { configuration: Configuration::A },
                PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
                PaverEvent::BootManagerFlush,
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
            NonCurrentConfiguration::A,
        )
        .await
        .unwrap();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware_foo contents"),
            &Image::join("firmware", "foo"),
            NonCurrentConfiguration::B,
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
            NonCurrentConfiguration::A,
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
                NonCurrentConfiguration::A,
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
                NonCurrentConfiguration::A,
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
                NonCurrentConfiguration::A,
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
            NonCurrentConfiguration::A,
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
                NonCurrentConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("vbmeta contents"),
                &Image::new("fuchsia.vbmeta"),
                NonCurrentConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi contents"),
                &Image::new("zbi"),
                NonCurrentConfiguration::B,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("vbmeta contents"),
                &Image::new("fuchsia.vbmeta"),
                NonCurrentConfiguration::B,
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
                NonCurrentConfiguration::B,
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

        flush(&data_sink, &boot_manager, NonCurrentConfiguration::A).await.unwrap();
        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::DataSinkFlush, PaverEvent::BootManagerFlush,]
        );

        flush(&data_sink, &boot_manager, NonCurrentConfiguration::B).await.unwrap();
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
    async fn query_non_current_configuration_returns_not_supported() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_current_configuration(&boot_manager)
                .await
                .unwrap()
                .to_non_current_configuration(),
            NonCurrentConfiguration::NotSupported
        );

        assert_eq!(paver.take_events(), vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_current_configuration_returns_not_supported() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        assert_eq!(
            query_current_configuration(&boot_manager).await.unwrap(),
            CurrentConfiguration::NotSupported
        );

        assert_eq!(paver.take_events(), vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_configuration_active_is_noop() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        set_configuration_active(&boot_manager, NonCurrentConfiguration::NotSupported)
            .await
            .unwrap();

        assert_eq!(paver.take_events(), vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ensure_current_partition_active_bails_out() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        ensure_current_partition_active(&boot_manager, CurrentConfiguration::NotSupported)
            .await
            .unwrap();

        // We shouldn't even get as far as finding out the active config.
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
            NonCurrentConfiguration::NotSupported,
        )
        .await
        .unwrap();

        write_image_buffer(
            &data_sink,
            make_buffer("the new vbmeta"),
            &Image::new("fuchsia.vbmeta"),
            NonCurrentConfiguration::NotSupported,
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
            NonCurrentConfiguration::NotSupported,
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
                NonCurrentConfiguration::NotSupported,
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
            NonCurrentConfiguration::NotSupported,
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
            NonCurrentConfiguration::NotSupported,
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
                NonCurrentConfiguration::NotSupported,
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

        flush(&data_sink, &boot_manager, NonCurrentConfiguration::NotSupported).await.unwrap();
        assert_eq!(paver.take_events(), vec![PaverEvent::DataSinkFlush,]);
    }
}
