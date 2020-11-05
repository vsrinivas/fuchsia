// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context},
    fidl_fuchsia_paver::{BootManagerProxy, Configuration},
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon::Status,
};

/// Inform the Paver service that Fuchsia booted successfully, so it marks the partition healthy
/// and stops decrementing the boot counter.
pub async fn set_active_configuration_healthy(
    boot_manager: &BootManagerProxy,
) -> Result<(), anyhow::Error> {
    // TODO(51480): This should use the "current" configuration, not active, when they differ.
    let active_config = match boot_manager
        .query_active_configuration()
        .await
        .map(|res| res.map_err(Status::from_raw))
    {
        Ok(Ok(active_config)) => active_config,
        Err(fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. }) => {
            fx_log_info!("ABR not supported");
            return Ok(());
        }
        Ok(Err(Status::NOT_SUPPORTED)) => {
            fx_log_info!("no partition is active; we're in recovery");
            return Ok(());
        }
        Err(e) => {
            return Err(e).context("transport error while calling query_active_configuration()")
        }
        Ok(Err(e)) => {
            return Err(e).context("paver error while calling query_active_configuration()")
        }
    };

    // Note: at this point, we know that ABR is supported.
    boot_manager
        .set_configuration_healthy(active_config)
        .await
        .map(Status::ok)
        .with_context(|| {
            format!("transport error while calling set_configuration_healthy({:?})", active_config)
        })?
        .with_context(|| {
            format!("paver error while calling set_configuration_healthy({:?})", active_config)
        })?;

    // Find out the inactive partition and mark it as unbootable.
    let inactive_config = match active_config {
        Configuration::A => Configuration::B,
        Configuration::B => Configuration::A,
        Configuration::Recovery => return Err(format_err!("Recovery should not be active")),
    };
    boot_manager
        .set_configuration_unbootable(inactive_config)
        .await
        .map(Status::ok)
        .with_context(|| {
            format!(
                "transport error while calling set_configuration_unbootable({:?})",
                inactive_config
            )
        })?
        .with_context(|| {
            format!("paver error while calling set_configuration_unbootable({:?})", inactive_config)
        })?;

    boot_manager
        .flush()
        .await
        .map(Status::ok)
        .context("transport error while calling flush()")?
        .context("paver error while calling flush()")?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_paver::Configuration,
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        mock_paver::{MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    // We should call SetConfigurationUnbootable when the device supports ABR and is not in recovery
    #[fasync::run_singlethreaded(test)]
    async fn test_calls_set_configuration_unbootable_config_a_active() {
        let paver_service =
            Arc::new(MockPaverServiceBuilder::new().active_config(Configuration::A).build());
        set_active_configuration_healthy(&paver_service.spawn_boot_manager_service())
            .await
            .expect("setting active succeeds");
        assert_eq!(
            paver_service.take_events(),
            vec![
                PaverEvent::QueryActiveConfiguration,
                PaverEvent::SetConfigurationHealthy { configuration: Configuration::A },
                PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
                PaverEvent::BootManagerFlush
            ]
        );
    }

    // We should call SetConfigurationUnbootable when the device supports ABR and is not in recovery
    #[fasync::run_singlethreaded(test)]
    async fn test_calls_set_configuration_unbootable_config_b_active() {
        let paver_service =
            Arc::new(MockPaverServiceBuilder::new().active_config(Configuration::B).build());
        set_active_configuration_healthy(&paver_service.spawn_boot_manager_service())
            .await
            .expect("setting active succeeds");
        assert_eq!(
            paver_service.take_events(),
            vec![
                PaverEvent::QueryActiveConfiguration,
                PaverEvent::SetConfigurationHealthy { configuration: Configuration::B },
                PaverEvent::SetConfigurationUnbootable { configuration: Configuration::A },
                PaverEvent::BootManagerFlush
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_device_does_not_support_abr() {
        let paver_service = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        set_active_configuration_healthy(&paver_service.spawn_boot_manager_service())
            .await
            .expect("setting active succeeds");
        assert_eq!(paver_service.take_events(), Vec::new());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_device_in_recovery() {
        let paver_service =
            Arc::new(MockPaverServiceBuilder::new().active_config(Configuration::Recovery).build());
        set_active_configuration_healthy(&paver_service.spawn_boot_manager_service())
            .await
            .expect("setting active succeeds");
        assert_eq!(paver_service.take_events(), vec![PaverEvent::QueryActiveConfiguration]);
    }
}
