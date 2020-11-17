// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_paver as paver, fuchsia_syslog::fx_log_info, fuchsia_zircon::Status,
    thiserror::Error,
};

/// Error condition that may be returned by check_and_set_system_health.
#[derive(Error, Debug)]
pub enum Error {
    #[error("health check failed")]
    HealthCheck(#[source] anyhow::Error),

    #[error("the current configuration ({_0:?}) is unbootable. This should never happen.")]
    CurrentConfigurationUnbootable(paver::Configuration),

    #[error("BootManager returned non-ok status while calling {method_name:}")]
    BootManagerStatus {
        method_name: &'static str,
        #[source]
        status: Status,
    },

    #[error("fidl error while calling BootManager method {method_name:}")]
    BootManagerFidl {
        method_name: &'static str,
        #[source]
        error: fidl::Error,
    },
}

/// Helper to convert fidl's nested errors.
trait BootManagerResultExt {
    type T;

    fn into_boot_manager_result(self, method_name: &'static str) -> Result<Self::T, Error>;
}

impl BootManagerResultExt for Result<i32, fidl::Error> {
    type T = ();

    fn into_boot_manager_result(
        self: Result<i32, fidl::Error>,
        method_name: &'static str,
    ) -> Result<(), Error> {
        match self.map(Status::ok) {
            Ok(Ok(())) => Ok(()),
            Ok(Err(status)) => Err(Error::BootManagerStatus { status, method_name }),
            Err(error) => Err(Error::BootManagerFidl { error, method_name }),
        }
    }
}

impl<T> BootManagerResultExt for Result<Result<T, i32>, fidl::Error> {
    type T = T;

    fn into_boot_manager_result(
        self: Result<Result<Self::T, i32>, fidl::Error>,
        method_name: &'static str,
    ) -> Result<Self::T, Error> {
        match self {
            Ok(Ok(value)) => Ok(value),
            Ok(Err(raw)) => {
                Err(Error::BootManagerStatus { status: Status::from_raw(raw), method_name })
            }
            Err(error) => Err(Error::BootManagerFidl { error, method_name }),
        }
    }
}

/// Puts BootManager metadata into a happy state, provided we believe the system can OTA.
///
/// The "happy state" is:
/// - The current configuration is active and marked Healthy.
/// - The alternate configuration is marked Unbootable.
///
/// First we decide whether the system is likely to be functional enough to apply an OTA:
/// - If the current configuration is marked Pending, we run a variety of checks and return
///   Error::HealthCheck if they fail.
/// - If the current configuration is marked Healthy, it means the system already passed the health
///   checks at some point. We assume they would still pass, and skip them.
/// - If the current configuration is marked Unbootable, and this function returns
///   Error::CurrentConfigurationUnbootable, because that should never happen.
///
/// Assuming we get through all that, we tell the paver to mark the current configuration Healthy
/// and the alternate configuration Unbootable.
///
/// As a special case, if the current configuration is Recovery, we return Ok without performing any
/// checks or making any changes.
///
/// If this returns an error, it likely means that the system is somehow busted, and that it should
/// be rebooted. Rebooting will hopefully either fix the issue or decrement the boot counter,
/// eventually leading to a rollback.
pub async fn check_and_set_system_health(
    boot_manager: &paver::BootManagerProxy,
) -> Result<(), Error> {
    let (current_config, alternate_config) = match boot_manager
        .query_current_configuration()
        .await
        .into_boot_manager_result("query_current_configuration")
    {
        Err(Error::BootManagerFidl {
            error: fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. },
            ..
        }) => {
            fx_log_info!("ABR not supported: skipping health checks and boot metadata updates");
            return Ok(());
        }
        Err(e) => return Err(e),

        Ok(paver::Configuration::Recovery) => {
            fx_log_info!("System in recovery: skipping health checks and boot metadata updates");
            return Ok(());
        }

        Ok(paver::Configuration::A) => (paver::Configuration::A, paver::Configuration::B),
        Ok(paver::Configuration::B) => (paver::Configuration::B, paver::Configuration::A),
    };

    // Note: at this point, we know that ABR is supported and we're not in Recovery.
    let current_config_status = boot_manager
        .query_configuration_status(current_config)
        .await
        .into_boot_manager_result("query_configuration_status")?;

    // Run the health checks if `current_config` isn't already marked `Healthy`, and pass any
    // failures up to the caller.
    match current_config_status {
        paver::ConfigurationStatus::Healthy => {
            fx_log_info!("current configuration is already healthy; skipping health checks");
        }
        paver::ConfigurationStatus::Unbootable => {
            return Err(Error::CurrentConfigurationUnbootable(current_config))
        }
        paver::ConfigurationStatus::Pending => {
            // Bail out if the system is unhealthy.
            let () = check_system_health().await.map_err(Error::HealthCheck)?;
        }
    }

    // Do all writes inside this function to ensure that we call flush no matter what.
    async fn internal_write(
        boot_manager: &paver::BootManagerProxy,
        current_config: paver::Configuration,
        alternate_config: paver::Configuration,
    ) -> Result<(), Error> {
        let () = boot_manager
            .set_configuration_healthy(current_config)
            .await
            .into_boot_manager_result("set_configuration_healthy")?;
        let () = boot_manager
            .set_configuration_unbootable(alternate_config)
            .await
            .into_boot_manager_result("set_configuration_unbootable")?;
        Ok(())
    }

    // Capture the result of the writes so we can return it after we flush.
    let write_result = internal_write(boot_manager, current_config, alternate_config).await;

    let () = boot_manager.flush().await.into_boot_manager_result("flush")?;

    write_result
}

/// Dummy function to indicate where health checks will eventually go, and how to handle associated
/// errors.
async fn check_system_health() -> Result<(), anyhow::Error> {
    Ok(())
}

#[cfg(test)]
mod tests {
    #![cfg(test)]
    use {
        super::*,
        fidl_fuchsia_paver::Configuration,
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        matches::assert_matches,
        mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    async fn run_with_healthy_current(
        current_config: Configuration,
        alternate_config: Configuration,
    ) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config)
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Healthy)))
                .build(),
        );
        check_and_set_system_health(&paver.spawn_boot_manager_service()).await.unwrap();
        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config },
                PaverEvent::SetConfigurationHealthy { configuration: current_config },
                PaverEvent::SetConfigurationUnbootable { configuration: alternate_config },
                PaverEvent::BootManagerFlush,
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_and_set_healthy_config_a() {
        run_with_healthy_current(Configuration::A, Configuration::B).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_and_set_healthy_config_b() {
        run_with_healthy_current(Configuration::B, Configuration::A).await
    }

    async fn run_with_pending_current(
        current_config: Configuration,
        alternate_config: Configuration,
    ) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config)
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .build(),
        );
        check_and_set_system_health(&paver.spawn_boot_manager_service()).await.unwrap();
        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config },
                // The health check gets performed here, but we don't see any side-effects.
                PaverEvent::SetConfigurationHealthy { configuration: current_config },
                PaverEvent::SetConfigurationUnbootable { configuration: alternate_config },
                PaverEvent::BootManagerFlush,
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_and_set_pending_config_a() {
        run_with_pending_current(Configuration::A, Configuration::B).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_and_set_pending_config_b() {
        run_with_pending_current(Configuration::B, Configuration::A).await
    }

    async fn run_with_unbootable_current(current_config: Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config)
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Unbootable)))
                .build(),
        );
        assert_matches!(
            check_and_set_system_health(&paver.spawn_boot_manager_service()).await,
            Err(Error::CurrentConfigurationUnbootable(cc))
            if cc == current_config
        );
        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_and_set_unbootable_config_a_returns_error() {
        run_with_unbootable_current(Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_and_set_unbootable_config_b_returns_error() {
        run_with_unbootable_current(Configuration::B).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_device_does_not_support_abr() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );

        check_and_set_system_health(&paver.spawn_boot_manager_service()).await.unwrap();
        assert_eq!(paver.take_events(), vec![]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_device_in_recovery() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(Configuration::Recovery)
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Healthy)))
                .build(),
        );
        check_and_set_system_health(&paver.spawn_boot_manager_service()).await.unwrap();
        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fails_when_set_healthy_fails() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .insert_hook(mphooks::return_error(|e| match e {
                    PaverEvent::SetConfigurationHealthy { .. } => Status::OUT_OF_RANGE,
                    _ => Status::OK,
                }))
                .build(),
        );

        assert_matches!(
            check_and_set_system_health(&paver.spawn_boot_manager_service()).await,
            Err(Error::BootManagerStatus {
                method_name: "set_configuration_healthy",
                status: Status::OUT_OF_RANGE
            })
        );
        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
                PaverEvent::SetConfigurationHealthy { configuration: Configuration::A },
                PaverEvent::BootManagerFlush,
            ]
        );
    }
}
