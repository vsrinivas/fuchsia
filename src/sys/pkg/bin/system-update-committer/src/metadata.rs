// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles interfacing with the boot metadata (e.g. checking a slot, committing a slot, etc).

use {
    check::do_health_checks,
    commit::do_commit,
    errors::MetadataError,
    fidl_fuchsia_paver as paver,
    fuchsia_zircon::{self as zx, AsHandleRef, EventPair, Peered},
    policy::PolicyEngine,
};

mod check;
mod commit;
mod configuration;
mod errors;
mod policy;

/// Puts BootManager metadata into a happy state, provided we believe the system can OTA.
///
/// The "happy state" is:
/// * The current configuration is active and marked Healthy.
/// * The alternate configuration is marked Unbootable.
///
/// To put the metadata in this state, we may need to check and commit. To make it easier to
/// determine if we should check and commit, we consult the `PolicyEngine`.
///
/// If this function returns an error, it likely means that the system is somehow busted, and that
/// it should be rebooted. Rebooting will hopefully either fix the issue or decrement the boot
/// counter, eventually leading to a rollback.
pub async fn put_metadata_in_happy_state(
    boot_manager: &paver::BootManagerProxy,
    p_check: &EventPair,
) -> Result<(), MetadataError> {
    let engine = PolicyEngine::build(boot_manager).await.map_err(MetadataError::Policy)?;
    if let Some(current_config) = engine.should_check_and_commit().map_err(MetadataError::Policy)? {
        // At this point, the FIDL server should start responding to requests so that clients can
        // find out that health checks are underway.
        let () = unblock_fidl_server(&p_check)?;
        let () = do_health_checks().await.map_err(MetadataError::HealthCheck)?;
        let () = do_commit(boot_manager, current_config).await.map_err(MetadataError::Commit)?;
    }

    // Tell the rest of the system we are now committed.
    let () = p_check
        .signal_peer(zx::Signals::NONE, zx::Signals::USER_0)
        .map_err(MetadataError::SignalPeer)?;

    // Ensure the FIDL server will be unblocked, even if we didn't run health checks.
    unblock_fidl_server(&p_check)
}

fn unblock_fidl_server(p_check: &zx::EventPair) -> Result<(), MetadataError> {
    p_check
        .signal_handle(zx::Signals::NONE, zx::Signals::USER_1)
        .map_err(MetadataError::SignalHandle)
}

// There is intentionally some overlap between the tests here and in `policy`. We do this so we can
// test the functionality at different layers.
#[cfg(test)]
mod tests {
    use {
        super::*,
        configuration::Configuration,
        fasync::OnSignals,
        fuchsia_async as fasync,
        fuchsia_zircon::{AsHandleRef, Status},
        mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    /// When we don't support ABR, we should not update metadata.
    /// However, the FIDL server should still be unblocked.
    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_device_does_not_support_abr() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let (p_check, p_fidl) = EventPair::create().unwrap();

        put_metadata_in_happy_state(&paver.spawn_boot_manager_service(), &p_check).await.unwrap();

        assert_eq!(paver.take_events(), vec![]);
        assert_eq!(
            p_fidl.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Ok(zx::Signals::USER_0)
        );
        assert_eq!(OnSignals::new(&p_check, zx::Signals::USER_1).await, Ok(zx::Signals::USER_1));
    }

    /// When we're in recovery, we should not update metadata.
    /// However, the FIDL server should still be unblocked.
    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_device_in_recovery() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(paver::Configuration::Recovery)
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Healthy)))
                .build(),
        );
        let (p_check, p_fidl) = EventPair::create().unwrap();

        put_metadata_in_happy_state(&paver.spawn_boot_manager_service(), &p_check).await.unwrap();

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
        assert_eq!(
            p_fidl.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Ok(zx::Signals::USER_0)
        );
        assert_eq!(OnSignals::new(&p_check, zx::Signals::USER_1).await, Ok(zx::Signals::USER_1));
    }

    /// When the current slot is healthy, we should not update metadata.
    /// However, the FIDL server should still be unblocked.
    async fn test_does_not_change_metadata_when_current_is_healthy(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Healthy)))
                .build(),
        );
        let (p_check, p_fidl) = EventPair::create().unwrap();

        put_metadata_in_happy_state(&paver.spawn_boot_manager_service(), &p_check).await.unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() }
            ]
        );
        assert_eq!(
            p_fidl.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Ok(zx::Signals::USER_0)
        );
        assert_eq!(OnSignals::new(&p_check, zx::Signals::USER_1).await, Ok(zx::Signals::USER_1));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_current_is_healthy_a() {
        test_does_not_change_metadata_when_current_is_healthy(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_current_is_healthy_b() {
        test_does_not_change_metadata_when_current_is_healthy(&Configuration::B).await
    }

    /// When the current slot is pending, we should check, commit, & unblock the fidl server.
    async fn test_checks_and_commits_when_current_is_pending(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .build(),
        );
        let (p_check, p_fidl) = EventPair::create().unwrap();

        put_metadata_in_happy_state(&paver.spawn_boot_manager_service(), &p_check).await.unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
                // The health check gets performed here, but we don't see any side-effects.
                PaverEvent::SetConfigurationHealthy { configuration: current_config.into() },
                PaverEvent::SetConfigurationUnbootable {
                    configuration: current_config.to_alternate().into()
                },
                PaverEvent::BootManagerFlush,
            ]
        );
        assert_eq!(OnSignals::new(&p_fidl, zx::Signals::USER_0).await, Ok(zx::Signals::USER_0));
        assert_eq!(OnSignals::new(&p_check, zx::Signals::USER_1).await, Ok(zx::Signals::USER_1));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_checks_and_commits_when_current_is_pending_a() {
        test_checks_and_commits_when_current_is_pending(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_checks_and_commits_when_current_is_pending_b() {
        test_checks_and_commits_when_current_is_pending(&Configuration::B).await
    }
}
