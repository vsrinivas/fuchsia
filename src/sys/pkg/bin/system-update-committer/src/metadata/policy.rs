// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::configuration::Configuration,
    super::errors::{
        BootManagerError, BootManagerResultExt, PolicyError, VerifyError, VerifyErrors,
        VerifySource,
    },
    crate::config::{Config as ComponentConfig, Mode},
    fidl_fuchsia_paver as paver,
    fuchsia_zircon::Status,
    tracing::info,
};

/// After gathering state from the BootManager, the PolicyEngine can answer whether we
/// should verify and commit.
#[derive(Debug)]
pub struct PolicyEngine {
    current_config: Option<(Configuration, paver::ConfigurationStatus)>,
}

impl PolicyEngine {
    /// Gathers system state from the BootManager.
    pub async fn build(boot_manager: &paver::BootManagerProxy) -> Result<Self, PolicyError> {
        let current_config = match boot_manager
            .query_current_configuration()
            .await
            .into_boot_manager_result("query_current_configuration")
        {
            // As a special case, if we don't support ABR, ensure we neither verify nor commit.
            Err(BootManagerError::Fidl {
                error: fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. },
                ..
            }) => {
                info!("ABR not supported: skipping health verification and boot metadata updates");
                return Ok(Self { current_config: None });
            }
            Err(e) => return Err(PolicyError::Build(e)),

            // As a special case, if we're in Recovery, ensure we neither verify nor commit.
            Ok(paver::Configuration::Recovery) => {
                info!("System in recovery: skipping health verification and boot metadata updates");
                return Ok(Self { current_config: None });
            }

            Ok(paver::Configuration::A) => Configuration::A,
            Ok(paver::Configuration::B) => Configuration::B,
        };

        let current_config_status = boot_manager
            .query_configuration_status((&current_config).into())
            .await
            .into_boot_manager_result("query_configuration_status")
            .map_err(PolicyError::Build)?;

        Ok(Self { current_config: Some((current_config, current_config_status)) })
    }

    /// Determines if we should verify and commit.
    /// * If we should (e.g. if the system is pending commit), return `Ok(Some(slot_to_act_on))`.
    /// * If we shouldn't (e.g. if the system is already committed), return `Ok(None)`.
    /// * If the system is seriously busted, return an error.
    pub fn should_verify_and_commit(&self) -> Result<Option<&Configuration>, PolicyError> {
        match self.current_config.as_ref() {
            Some((config, paver::ConfigurationStatus::Pending)) => Ok(Some(config)),
            Some((config, paver::ConfigurationStatus::Unbootable)) => {
                Err(PolicyError::CurrentConfigurationUnbootable(config.into()))
            }
            // We don't need to verify and commit in these situations because it wont give us any
            // new information or have any effect on boot metadata:
            // * ABR is not supported
            // * current slot is recovery
            // * current slot already healthy on boot
            None | Some((_, paver::ConfigurationStatus::Healthy)) => Ok(None),
        }
    }

    /// Filters out any failed verifications if the config says to ignore them.
    pub fn apply_config(
        res: Result<(), VerifyErrors>,
        config: &ComponentConfig,
    ) -> Result<(), VerifyErrors> {
        match res {
            Ok(()) => Ok(()),
            Err(VerifyErrors::VerifyErrors(v)) => {
                // For any existing verification errors,
                let errors: Vec<VerifyError> = v
                    .into_iter()
                    .filter(|VerifyError::VerifyError(source, _)| {
                        // filter out the ones which config says to ignore.
                        match source {
                            VerifySource::Blobfs => config.blobfs() != &Mode::Ignore,
                        }
                    })
                    .collect();

                // If there are any remaining verification errors, pass them on.
                if errors.is_empty() {
                    Ok(())
                } else {
                    Err(VerifyErrors::VerifyErrors(errors))
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::metadata::errors::VerifyFailureReason,
        assert_matches::assert_matches,
        fidl_fuchsia_update_verify as verify, fuchsia_async as fasync,
        fuchsia_zircon::Status,
        mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    /// Test we should NOT verify and commit when when the device is in recovery.
    #[fasync::run_singlethreaded(test)]
    async fn test_skip_when_device_in_recovery() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(paver::Configuration::Recovery)
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Healthy)))
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert_eq!(engine.should_verify_and_commit().unwrap(), None);

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
    }

    /// Test we should NOT verify and commit when the device does not support ABR.
    #[fasync::run_singlethreaded(test)]
    async fn test_skip_when_device_does_not_support_abr() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert_eq!(engine.should_verify_and_commit().unwrap(), None);

        assert_eq!(paver.take_events(), vec![]);
    }

    /// Helper fn to verify we should NOT verify and commit when current is healthy.
    async fn test_skip_when_current_is_healthy(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Healthy)))
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert_eq!(engine.should_verify_and_commit().unwrap(), None);

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_skip_when_current_is_healthy_a() {
        test_skip_when_current_is_healthy(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_skip_when_current_is_healthy_b() {
        test_skip_when_current_is_healthy(&Configuration::B).await
    }

    /// Helper fn to verify we should verify and commit when current is pending.
    async fn test_verify_and_commit_when_current_is_pending(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert_eq!(engine.should_verify_and_commit().unwrap(), Some(current_config));

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_verify_and_commit_when_current_is_pending_a() {
        test_verify_and_commit_when_current_is_pending(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_verify_and_commit_when_current_is_pending_b() {
        test_verify_and_commit_when_current_is_pending(&Configuration::B).await
    }

    /// Helper fn to verify an error is returned if current is unbootable.
    async fn test_returns_error_when_current_unbootable(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Unbootable)))
                .build(),
        );
        let engine = PolicyEngine::build(&paver.spawn_boot_manager_service()).await.unwrap();

        assert_matches!(
            engine.should_verify_and_commit(),
            Err(PolicyError::CurrentConfigurationUnbootable(cc)) if cc == current_config.into()
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
            Err(PolicyError::Build(BootManagerError::Status {
                method_name: "query_current_configuration",
                status: Status::OUT_OF_RANGE
            }))
        );

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
    }

    fn test_blobfs_verify_errors(config: ComponentConfig, expect_err: bool) {
        let timeout_err = Err(VerifyErrors::VerifyErrors(vec![VerifyError::VerifyError(
            VerifySource::Blobfs,
            VerifyFailureReason::Timeout,
        )]));
        let verify_err = Err(VerifyErrors::VerifyErrors(vec![VerifyError::VerifyError(
            VerifySource::Blobfs,
            VerifyFailureReason::Verify(verify::VerifyError::Internal),
        )]));
        let fidl_err = Err(VerifyErrors::VerifyErrors(vec![VerifyError::VerifyError(
            VerifySource::Blobfs,
            VerifyFailureReason::Fidl(fidl::Error::OutOfRange),
        )]));

        assert_eq!(PolicyEngine::apply_config(timeout_err, &config).is_err(), expect_err);
        assert_eq!(PolicyEngine::apply_config(verify_err, &config).is_err(), expect_err);
        assert_eq!(PolicyEngine::apply_config(fidl_err, &config).is_err(), expect_err);
    }

    /// Blobfs errors should be ignored if the config says so.
    #[test]
    fn test_blobfs_errors_ignored() {
        test_blobfs_verify_errors(ComponentConfig::builder().blobfs(Mode::Ignore).build(), false);
    }

    #[test]
    fn test_errors_all_ignored() {
        let ve1 = VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Timeout);
        let ve2 = VerifyError::VerifyError(
            VerifySource::Blobfs,
            VerifyFailureReason::Verify(verify::VerifyError::Internal),
        );

        let config = ComponentConfig::builder().blobfs(Mode::Ignore).build();

        // TODO(https://fxbug.dev/76636): When there are multiple VerifySource
        // types, test heterogeneous VerifyErrors lists.
        assert_matches!(
            PolicyEngine::apply_config(Err(VerifyErrors::VerifyErrors(vec![ve1, ve2])), &config),
            Ok(())
        );
    }

    #[test]
    fn test_errors_none_ignored() {
        let ve1 = VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Timeout);
        let ve2 = VerifyError::VerifyError(
            VerifySource::Blobfs,
            VerifyFailureReason::Verify(verify::VerifyError::Internal),
        );

        let config = ComponentConfig::builder().blobfs(Mode::RebootOnFailure).build();

        let filtered_errors = assert_matches!(
            PolicyEngine::apply_config(Err(VerifyErrors::VerifyErrors(vec![ve1, ve2])), &config),
            Err(VerifyErrors::VerifyErrors(s)) => s);

        assert_matches!(
            &filtered_errors[..],
            [
                VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Timeout),
                VerifyError::VerifyError(
                    VerifySource::Blobfs,
                    VerifyFailureReason::Verify(verify::VerifyError::Internal)
                )
            ]
        );
    }

    /// Blobfs errors should NOT be ignored if the config says to reboot on failure.
    #[test]
    fn test_blobfs_errors_reboot_on_failure() {
        test_blobfs_verify_errors(
            ComponentConfig::builder().blobfs(Mode::RebootOnFailure).build(),
            true,
        );
    }
}
