// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_paver::{
        Asset, BootManagerMarker, BootManagerProxy, Configuration, ConfigurationStatus,
        DataSinkMarker, DataSinkProxy, PaverMarker, WriteFirmwareResult,
    },
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    fuchsia_zircon::{Status, VmoChildOptions},
    thiserror::Error,
    update_package::{Image, ImageClass, UpdatePackage},
};

mod configuration;
use configuration::TargetConfiguration;
pub use configuration::{CurrentConfiguration, NonCurrentConfiguration};

#[derive(Debug, Error)]
pub enum WriteAssetError {
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

pub async fn paver_read_firmware(
    data_sink: &DataSinkProxy,
    configuration: Configuration,
    subtype: &str,
) -> Result<Buffer, Error> {
    let result = data_sink
        .read_firmware(configuration, subtype)
        .await
        .context("while performing the read_firmware call")?;
    let buffer = result
        .map_err(|status| anyhow!("read_firmware responded with {}", Status::from_raw(status)))?;
    Ok(buffer)
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
    let target = match image.classify() {
        ImageClass::Zbi => ImageTarget::Asset {
            asset: Asset::Kernel,
            configuration: desired_config.to_target_configuration(),
        },
        ImageClass::ZbiVbmeta => ImageTarget::Asset {
            asset: Asset::VerifiedBootMetadata,
            configuration: desired_config.to_target_configuration(),
        },
        ImageClass::Recovery => ImageTarget::Asset {
            asset: Asset::Kernel,
            configuration: TargetConfiguration::Single(Configuration::Recovery),
        },
        ImageClass::RecoveryVbmeta => ImageTarget::Asset {
            asset: Asset::VerifiedBootMetadata,
            configuration: TargetConfiguration::Single(Configuration::Recovery),
        },
        ImageClass::Firmware => ImageTarget::Firmware {
            subtype: image.subtype().unwrap_or(""),
            configuration: desired_config.to_target_configuration(),
        },
    };

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
                VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE | VmoChildOptions::RESIZABLE,
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

pub async fn write_image_buffer(
    data_sink: &DataSinkProxy,
    buffer: Buffer,
    image: &Image,
    desired_config: NonCurrentConfiguration,
) -> Result<(), Error> {
    let target = classify_image(image, desired_config)?;
    let payload = Payload { display_name: image.name(), buffer };

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
    let paver = fuchsia_component::client::connect_to_protocol::<PaverMarker>()
        .context("connect to fuchsia.paver.Paver")?;

    let (data_sink, server_end) = fidl::endpoints::create_proxy::<DataSinkMarker>()?;
    let () = paver.find_data_sink(server_end).context("connect to fuchsia.paver.DataSink")?;

    let (boot_manager, server_end) = fidl::endpoints::create_proxy::<BootManagerMarker>()?;
    let () = paver.find_boot_manager(server_end).context("connect to fuchsia.paver.BootManager")?;

    Ok((data_sink, boot_manager))
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
    match boot_manager.query_configuration_status(configuration).await {
        Ok(Ok(configuration_status)) => Ok(configuration_status),
        Ok(Err(status)) => {
            Err(anyhow!("query_configuration_status responded with {}", Status::from_raw(status)))
        }
        Err(err) => Err(anyhow!(err).context("while performing query_configuration_status call")),
    }
}

/// Error conditions possibly returned by prepare_partition_metadata.
#[derive(Debug, Error)]
pub enum PreparePartitionMetadataError {
    #[error(
        "current configuration ({current_configuration:?}) is not Healthy \
        (status = {current_configuration_status:?}). Refusing to perform update."
    )]
    CurrentConfigurationUnhealthy {
        current_configuration: Configuration,
        current_configuration_status: ConfigurationStatus,
    },

    #[error(transparent)]
    Other(#[from] Error),
}

/// Ensure that the partition boot metadata is in a state where we're ready to write the update.
///
/// Specifically, this means:
/// - The current partition must be marked as Healthy. If it isn't, this function returns an error.
/// - The non-current partition must be marked as Unbootable. If it isn't, this function will mark
///   it as Unbootable and flush the BootManager.
///
/// As a side effect of marking non-current Unbootable, current will be made Active (if it wasn't
/// already).
///
/// If the result is OK, this function returns the current partition. The update should be written
/// to the current partition's respective non-current partition. If the result is an error, the
/// update should be aborted. Each time we return an error in the code below we justify why the
/// update shouldn't continue.
///
/// This operation can fail, and it is very important that all states it can end in (by returning)
/// don't end in a bricked device when the update continues. Some devices use BootManager's flush to
/// flush operations, and some don't. This function needs to support both strategies.
pub async fn prepare_partition_metadata(
    boot_manager: &BootManagerProxy,
) -> Result<CurrentConfiguration, PreparePartitionMetadataError> {
    // ERROR JUSTIFICATION: If we can't get the current configuration, we won't know where to write
    // the update.
    let current = query_current_configuration(boot_manager)
        .await
        .context("while querying current configuration")?;

    let current_configuration = match current.to_configuration() {
        None => {
            fx_log_info!("ABR not supported, no partition preparation necessary");
            return Ok(CurrentConfiguration::NotSupported);
        }
        Some(current_configuration) => current_configuration,
    };

    // ERROR JUSTIFICATION: We only want to write the update if `current` is `Healthy` (see below),
    // so if we can't determine its status, we don't know if we want to continue.
    let current_configuration_status =
        paver_query_configuration_status(boot_manager, current_configuration)
            .await
            .context("while querying current configuration status")?;

    match current_configuration_status {
        // It's the responsibility of the caller who triggered the update to ensure `current_config`
        // is Healthy (probably by calling `system_health_check::check_and_set_system_health()`
        // before we'll apply an update. If they didn't, bail out.
        //
        // ERROR JUSTIFICATION: We want to be confident the OTA will succeed before throwing away
        // our rollback target in the non-current configuration. (Note: In almost all circumstances,
        // the non-current configuration will be bootable at this point, because we never mark it
        // `Unbootable` until after `current_config` has been marked `Healthy`). Thus, we refuse to
        // update unless `current_config` has been marked `Healthy`.
        ConfigurationStatus::Pending | ConfigurationStatus::Unbootable => {
            Err(PreparePartitionMetadataError::CurrentConfigurationUnhealthy {
                current_configuration,
                current_configuration_status,
            })
        }
        // If the current configuration is Healthy, we mark the non-current configuration as
        // Unbootable.
        ConfigurationStatus::Healthy => {
            // We need to flush the boot manager regardless, but if that operation succeeded while
            // set_non_current_configuration_unbootable failed, propagate the error.
            //
            // ERROR JUSTIFICATION: the non-current configuration might be active, meaning it'll be
            // used on the next reboot. If we don't mark it Unbootable, and we reboot halfway
            // through writing the update for some reason, we'll reboot into a totally broken
            // system.
            let set_unbootable_result = set_non_current_configuration_unbootable(
                boot_manager,
                current.to_non_current_configuration(),
            )
            .await
            .context("while setting non-current configuration unbootable");

            // ERROR JUSTIFICATION: Same as above; we want to make sure marking it Unbootable goes
            // through.
            let () = paver_flush_boot_manager(boot_manager)
                .await
                .context("while flushing boot manager")?;

            let () = set_unbootable_result?;
            Ok(current)
        }
    }
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

/// Set a non-current configuration as unbootable. Dangerous! If ABR is not supported, return an
/// error.
async fn set_non_current_configuration_unbootable(
    boot_manager: &BootManagerProxy,
    configuration: NonCurrentConfiguration,
) -> Result<(), Error> {
    if let Some(configuration) = configuration.to_configuration() {
        set_arbitrary_configuration_unbootable(boot_manager, configuration).await
    } else {
        Err(anyhow!("could not set non-current configuration unbootable: {:?}", configuration))
    }
}

/// Set an arbitrary configuration as unbootable. Extra dangerous!
async fn set_arbitrary_configuration_unbootable(
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
    let () = set_arbitrary_configuration_unbootable(boot_manager, Configuration::A)
        .await
        .context("while marking Configuration::A unbootable")?;
    let () = set_arbitrary_configuration_unbootable(boot_manager, Configuration::B)
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
        .open_image(image)
        .await
        .with_context(|| format!("error opening {}", image.name()))?;

    fx_log_info!("writing {} from update package", image.name());

    let res = write_image_buffer(data_sink, buffer, image, desired_config)
        .await
        .with_context(|| format!("error writing {}", image.name()));

    if let Ok(()) = &res {
        fx_log_info!("wrote {} successfully", image.name());
    }
    res
}

pub async fn paver_flush_boot_manager(boot_manager: &BootManagerProxy) -> Result<(), Error> {
    let () = Status::ok(
        boot_manager
            .flush()
            .await
            .context("while performing fuchsia.paver.BootManager/Flush call")?,
    )
    .context("fuchsia.paver.BootManager/Flush responded with")?;
    Ok(())
}

pub async fn paver_flush_data_sink(data_sink: &DataSinkProxy) -> Result<(), Error> {
    let () = Status::ok(
        data_sink.flush().await.context("while performing fuchsia.paver.DataSink/Flush call")?,
    )
    .context("fuchsia.paver.DataSink/Flush responded with")?;
    Ok(())
}

#[cfg(test)]
fn make_buffer(contents: impl AsRef<[u8]>) -> Buffer {
    use fuchsia_zircon::{Vmo, VmoOptions};
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
        assert_matches::assert_matches,
        mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
        update_package::ImageType,
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
    async fn flush_boot_manager_makes_call() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        paver_flush_boot_manager(&boot_manager).await.unwrap();

        assert_eq!(paver.take_events(), vec![PaverEvent::BootManagerFlush]);
    }

    async fn assert_prepare_partition_metadata_bails_out_with_unhealthy_current(
        current_config: Configuration,
        status: ConfigurationStatus,
    ) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config)
                .insert_hook(mphooks::config_status(move |_| Ok(status)))
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        assert_matches!(prepare_partition_metadata(&boot_manager).await,
            Err(PreparePartitionMetadataError::CurrentConfigurationUnhealthy{
                current_configuration: cc,
                current_configuration_status: ccs,
            })
            if cc == current_config && ccs == status
        );
        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prepare_partition_metadata_bails_out_if_current_pending_a() {
        assert_prepare_partition_metadata_bails_out_with_unhealthy_current(
            Configuration::A,
            ConfigurationStatus::Pending,
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prepare_partition_metadata_bails_out_if_current_unbootable_a() {
        assert_prepare_partition_metadata_bails_out_with_unhealthy_current(
            Configuration::A,
            ConfigurationStatus::Unbootable,
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prepare_partition_metadata_bails_out_if_current_pending_b() {
        assert_prepare_partition_metadata_bails_out_with_unhealthy_current(
            Configuration::B,
            ConfigurationStatus::Pending,
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prepare_partition_metadata_bails_out_if_current_unbootable_b() {
        assert_prepare_partition_metadata_bails_out_with_unhealthy_current(
            Configuration::B,
            ConfigurationStatus::Unbootable,
        )
        .await;
    }

    async fn assert_successful_prepare_partition_metadata(
        current_config: Configuration,
        target_config: Configuration,
    ) {
        let paver = Arc::new(MockPaverServiceBuilder::new().current_config(current_config).build());
        let boot_manager = paver.spawn_boot_manager_service();

        prepare_partition_metadata(&boot_manager).await.unwrap();
        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config },
                PaverEvent::SetConfigurationUnbootable { configuration: target_config },
                PaverEvent::BootManagerFlush,
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prepare_partition_metadata_targets_b_in_config_a() {
        assert_successful_prepare_partition_metadata(Configuration::A, Configuration::B).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prepare_partition_metadata_targets_a_in_config_b() {
        assert_successful_prepare_partition_metadata(Configuration::B, Configuration::A).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prepare_partition_metadata_targets_a_in_config_r() {
        assert_successful_prepare_partition_metadata(Configuration::Recovery, Configuration::A)
            .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn prepare_partition_metadata_does_nothing_if_abr_not_supported() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let boot_manager = paver.spawn_boot_manager_service();

        prepare_partition_metadata(&boot_manager).await.unwrap();
        assert_eq!(paver.take_events(), vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_firmware() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware contents"),
            &Image::new(ImageType::Bootloader, None),
            NonCurrentConfiguration::A,
        )
        .await
        .unwrap();

        let image = &Image::new(ImageType::Firmware, Some("foo"));

        write_image_buffer(
            &data_sink,
            make_buffer("firmware_foo contents"),
            image,
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
                .insert_hook(mphooks::write_firmware(|_, _, _| {
                    WriteFirmwareResult::Unsupported(true)
                }))
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware of the future!"),
            &Image::new(ImageType::Firmware, Some("unknown")),
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
                .insert_hook(mphooks::write_firmware(|_, _, _| {
                    WriteFirmwareResult::Status(Status::INTERNAL.into_raw())
                }))
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("oops"),
                &Image::new(ImageType::Firmware, None),
                NonCurrentConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("firmware failed to write")
        );

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteFirmware {
                configuration: Configuration::A,
                firmware_type: "".to_owned(),
                payload: b"oops".to_vec()
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_asset_to_single_config() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi contents"),
            &Image::new(ImageType::Zbi, None),
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
                .insert_hook(mphooks::return_error(|event| match event {
                    PaverEvent::WriteAsset { .. } => Status::INTERNAL,
                    _ => panic!("Unexpected event: {:?}", event),
                }))
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi contents"),
                &Image::new(ImageType::Zbi, None),
                NonCurrentConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("vbmeta contents"),
                &Image::new(ImageType::FuchsiaVbmeta, None),
                NonCurrentConfiguration::A,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi contents"),
                &Image::new(ImageType::Zbi, None),
                NonCurrentConfiguration::B,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("vbmeta contents"),
                &Image::new(ImageType::FuchsiaVbmeta, None),
                NonCurrentConfiguration::B,
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn paver_flush_boot_manager_makes_calls() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let boot_manager = paver.spawn_boot_manager_service();

        paver_flush_boot_manager(&boot_manager).await.unwrap();
        assert_eq!(paver.take_events(), vec![PaverEvent::BootManagerFlush,]);
    }
}

#[cfg(test)]
mod abr_not_supported_tests {
    use {
        super::*,
        assert_matches::assert_matches,
        mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
        update_package::ImageType,
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
    async fn write_image_buffer_writes_asset_to_both_configs() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi.signed contents"),
            &Image::new(ImageType::ZbiSigned, None),
            NonCurrentConfiguration::NotSupported,
        )
        .await
        .unwrap();

        write_image_buffer(
            &data_sink,
            make_buffer("the new vbmeta"),
            &Image::new(ImageType::FuchsiaVbmeta, None),
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
            &Image::new(ImageType::Firmware, None),
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
                .insert_hook(mphooks::return_error(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::A,
                        asset: Asset::Kernel,
                        payload: _,
                    } => Status::INTERNAL,
                    _ => Status::OK,
                }))
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi.signed contents"),
                &Image::new(ImageType::ZbiSigned, None),
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
                .insert_hook(mphooks::return_error(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::B,
                        asset: Asset::Kernel,
                        payload: _,
                    } => Status::NOT_SUPPORTED,
                    _ => Status::OK,
                }))
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi.signed contents"),
            &Image::new(ImageType::ZbiSigned, None),
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
                .insert_hook(mphooks::write_firmware(|configuration, _, _| match configuration {
                    Configuration::B => WriteFirmwareResult::Unsupported(true),
                    _ => WriteFirmwareResult::Status(Status::OK.into_raw()),
                }))
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware contents"),
            &Image::new(ImageType::Firmware, None),
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
                .insert_hook(mphooks::return_error(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::B,
                        asset: Asset::Kernel,
                        ..
                    } => Status::INTERNAL,
                    _ => Status::OK,
                }))
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi.signed contents"),
                &Image::new(ImageType::ZbiSigned, None),
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
    async fn paver_flush_boot_manager_doesnt_makes_calls() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );

        let boot_manager = paver.spawn_boot_manager_service();

        assert_matches!(paver_flush_boot_manager(&boot_manager).await, Err(_));
    }
}
