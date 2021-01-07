// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::configuration::Configuration,
    super::errors::{BootManagerError, BootManagerResultExt, DetermineSlotToCommitError},
    fidl_fuchsia_paver as paver,
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon::Status,
};

/// After gathering state from the BootManager, the PolicyEngine can answer questions like:
/// * should we run health checks?
/// * should we do a commit?
#[derive(Debug)]
pub struct PolicyEngine {
    current_config: Option<(Configuration, paver::ConfigurationStatus)>,
}

impl PolicyEngine {
    /// Gathers system state from the BootManager.
    pub async fn build(boot_manager: &paver::BootManagerProxy) -> Result<Self, BootManagerError> {
        let current_config = match boot_manager
            .query_current_configuration()
            .await
            .into_boot_manager_result("query_current_configuration")
        {
            // As a special case, if we don't support ABR, ensure we neither check nor commit.
            Err(BootManagerError::Fidl {
                error: fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. },
                ..
            }) => {
                fx_log_info!("ABR not supported: skipping health checks and boot metadata updates");
                return Ok(Self { current_config: None });
            }
            Err(e) => return Err(e),

            // As a special case, if we're in Recovery, ensure we neither check nor commit.
            Ok(paver::Configuration::Recovery) => {
                fx_log_info!(
                    "System in recovery: skipping health checks and boot metadata updates"
                );
                return Ok(Self { current_config: None });
            }

            Ok(paver::Configuration::A) => Configuration::A,
            Ok(paver::Configuration::B) => Configuration::B,
        };

        let current_config_status = boot_manager
            .query_configuration_status((&current_config).into())
            .await
            .into_boot_manager_result("query_configuration_status")?;

        Ok(Self { current_config: Some((current_config, current_config_status)) })
    }

    /// Determines if we should do health checks.
    /// * We should only run health checks when the current config is Pending.
    /// * If the current config is Healthy, it means the system already passed the health checks at
    ///   some point. We assume they would still pass so we skip them.
    /// * If the current config is Unbootable, we also skip the checks (this will surface as an
    ///   error in `determine_slot_to_commit`).
    pub fn should_check(&self) -> bool {
        match self.current_config {
            Some((_, paver::ConfigurationStatus::Pending)) => true,
            _ => false,
        }
    }

    /// Determines which slot to commit (e.g. modify the boot metadata), if any.
    pub fn determine_slot_to_commit(
        &self,
    ) -> Result<Option<&Configuration>, DetermineSlotToCommitError> {
        match self.current_config.as_ref() {
            Some((config, paver::ConfigurationStatus::Pending))
            // FIXME: we shouldn't need to commit if the current slot is already healthy.
            | Some((config, paver::ConfigurationStatus::Healthy)) => Ok(Some(config)),
            Some((config, paver::ConfigurationStatus::Unbootable)) => {
                Err(DetermineSlotToCommitError::CurrentConfigurationUnbootable(
                    config.into(),
                ))
            }
            // ABR is not supported or current slot is recovery, so we shouldn't actually commit.
            None => Ok(None),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        matches::assert_matches,
        mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    /// Test we neither check nor commit when the device is in recovery.
    #[fasync::run_singlethreaded(test)]
    async fn test_skip_when_device_in_recovery() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(paver::Configuration::Recovery)
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Healthy)))
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert!(!engine.should_check());
        assert_eq!(engine.determine_slot_to_commit().unwrap(), None);

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
    }

    /// Test we neither check nor commit when the device does not support ABR.
    #[fasync::run_singlethreaded(test)]
    async fn test_skip_when_device_does_not_support_abr() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert!(!engine.should_check());
        assert_eq!(engine.determine_slot_to_commit().unwrap(), None);

        assert_eq!(paver.take_events(), vec![]);
    }

    /// Helper fn to verify we commit (but don't check) when current is healthy.
    async fn test_only_commits_when_current_is_healthy(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Healthy)))
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert!(!engine.should_check());
        assert_eq!(engine.determine_slot_to_commit().unwrap(), Some(current_config));

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_only_commits_when_current_is_healthy_a() {
        test_only_commits_when_current_is_healthy(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_only_commits_when_current_is_healthy_b() {
        test_only_commits_when_current_is_healthy(&Configuration::B).await
    }

    /// Helper fn to verify we both check and commit when current is pending.
    async fn test_checks_and_commits_when_current_is_pending(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert!(engine.should_check());
        assert_eq!(engine.determine_slot_to_commit().unwrap(), Some(current_config));

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_checks_and_commits_when_current_is_pending_a() {
        test_checks_and_commits_when_current_is_pending(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_checks_and_commits_when_current_is_pending_b() {
        test_checks_and_commits_when_current_is_pending(&Configuration::B).await
    }

    /// Helper fn to verify an error is returned on `determine_slot_to_commit` if current is unbootable.
    async fn test_returns_error_when_current_unbootable(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Unbootable)))
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert!(!engine.should_check());
        assert_matches!(
            engine.determine_slot_to_commit(),
            Err(DetermineSlotToCommitError::CurrentConfigurationUnbootable(cc))
            if cc == current_config.into()
        );

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_returns_error_when_current_unbootable_a() {
        test_returns_error_when_current_unbootable(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_returns_error_when_current_unbootable_b() {
        test_returns_error_when_current_unbootable(&Configuration::B).await
    }

    /// Test the build fn fails on a standard paver error.
    #[fasync::run_singlethreaded(test)]
    async fn test_build_fails_when_paver_fails() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .insert_hook(mphooks::return_error(|e| match e {
                    PaverEvent::QueryCurrentConfiguration { .. } => Status::OUT_OF_RANGE,
                    _ => Status::OK,
                }))
                .build(),
        );

        assert_matches!(
            PolicyEngine::build(&paver.spawn_boot_manager_service()).await,
            Err(BootManagerError::Status {
                method_name: "query_current_configuration",
                status: Status::OUT_OF_RANGE
            })
        );

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
    }
}
