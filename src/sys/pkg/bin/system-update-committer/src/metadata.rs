// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles interfacing with the boot metadata (e.g. verifying a slot, committing a slot, etc).

use {
    crate::config::Config,
    crate::metadata::verify::VerifierProxy,
    commit::do_commit,
    errors::MetadataError,
    fidl_fuchsia_paver as paver, fuchsia_inspect as finspect,
    fuchsia_zircon::{self as zx, EventPair, Peered},
    futures::channel::oneshot,
    policy::PolicyEngine,
    verify::do_health_verification,
};

mod commit;
mod configuration;
mod errors;
mod inspect;
mod policy;
mod verify;

/// Puts BootManager metadata into a happy state, provided we believe the system can OTA.
///
/// The "happy state" is:
/// * The current configuration is active and marked Healthy.
/// * The alternate configuration is marked Unbootable.
///
/// To put the metadata in this state, we may need to verify and commit. To make it easier to
/// determine if we should verify and commit, we consult the `PolicyEngine`.
///
/// If this function returns an error, it likely means that the system is somehow busted, and that
/// it should be rebooted. Rebooting will hopefully either fix the issue or decrement the boot
/// counter, eventually leading to a rollback.
pub async fn put_metadata_in_happy_state(
    boot_manager: &paver::BootManagerProxy,
    p_internal: &EventPair,
    unblocker: oneshot::Sender<()>,
    verifiers: &[&dyn VerifierProxy],
    node: &finspect::Node,
    config: &Config,
) -> Result<(), MetadataError> {
    let mut unblocker = Some(unblocker);
    if config.enable() {
        let engine = PolicyEngine::build(boot_manager).await.map_err(MetadataError::Policy)?;
        if let Some(current_config) =
            engine.should_verify_and_commit().map_err(MetadataError::Policy)?
        {
            // At this point, the FIDL server should start responding to requests so that clients can
            // find out that the health verification is underway.
            unblocker = unblock_fidl_server(unblocker)?;
            let res = do_health_verification(verifiers, node).await;
            let () = PolicyEngine::apply_config(res, config).map_err(MetadataError::Verify)?;
            let () =
                do_commit(boot_manager, current_config).await.map_err(MetadataError::Commit)?;
        }
    }

    // Tell the rest of the system we are now committed.
    let () = p_internal
        .signal_peer(zx::Signals::NONE, zx::Signals::USER_0)
        .map_err(MetadataError::SignalPeer)?;

    // Ensure the FIDL server will be unblocked, even if we didn't verify health.
    unblock_fidl_server(unblocker)?;

    Ok(())
}

fn unblock_fidl_server(
    unblocker: Option<oneshot::Sender<()>>,
) -> Result<Option<oneshot::Sender<()>>, MetadataError> {
    if let Some(sender) = unblocker {
        let () = sender.send(()).map_err(|_| MetadataError::Unblock)?;
    }
    Ok(None)
}

// There is intentionally some overlap between the tests here and in `policy`. We do this so we can
// test the functionality at different layers.
#[cfg(test)]
mod tests {
    use {
        super::errors::{VerifyError, VerifyErrors, VerifyFailureReason, VerifySource},
        super::*,
        crate::config::Mode,
        ::fidl::endpoints::create_proxy,
        assert_matches::assert_matches,
        configuration::Configuration,
        fasync::OnSignals,
        fidl_fuchsia_update_verify as fidl,
        fidl_fuchsia_update_verify::BlobfsVerifierProxy,
        fuchsia_async as fasync,
        fuchsia_zircon::{AsHandleRef, Status},
        mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
        mock_verifier::MockVerifierService,
        std::sync::{
            atomic::{AtomicU32, Ordering},
            Arc,
        },
    };

    fn blobfs_verifier_and_call_count(
        res: Result<(), fidl::VerifyError>,
    ) -> (BlobfsVerifierProxy, Arc<AtomicU32>) {
        let call_count = Arc::new(AtomicU32::new(0));
        let call_count_clone = Arc::clone(&call_count);
        let verifier = Arc::new(MockVerifierService::new(move |_| {
            call_count_clone.fetch_add(1, Ordering::SeqCst);
            res
        }));

        let (blobfs_verifier, server) = verifier.spawn_blobfs_verifier_service();
        let () = server.detach();

        (blobfs_verifier, call_count)
    }

    fn success_blobfs_verifier_and_call_count() -> (BlobfsVerifierProxy, Arc<AtomicU32>) {
        blobfs_verifier_and_call_count(Ok(()))
    }

    fn failing_blobfs_verifier_and_call_count() -> (BlobfsVerifierProxy, Arc<AtomicU32>) {
        blobfs_verifier_and_call_count(Err(fidl::VerifyError::Internal))
    }

    /// When we don't support ABR, we should not update metadata.
    /// However, the FIDL server should still be unblocked.
    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_device_does_not_support_abr() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(Status::NOT_SUPPORTED)
                .build(),
        );
        let (p_internal, p_external) = EventPair::create().unwrap();
        let (unblocker, unblocker_recv) = oneshot::channel();
        let (blobfs_verifier, blobfs_verifier_call_count) =
            success_blobfs_verifier_and_call_count();

        put_metadata_in_happy_state(
            &paver.spawn_boot_manager_service(),
            &p_internal,
            unblocker,
            &[&blobfs_verifier],
            &finspect::Node::default(),
            &Config::builder().build(),
        )
        .await
        .unwrap();

        assert_eq!(paver.take_events(), vec![]);
        assert_eq!(
            p_external.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Ok(zx::Signals::USER_0)
        );
        assert_eq!(unblocker_recv.await, Ok(()));
        assert_eq!(blobfs_verifier_call_count.load(Ordering::SeqCst), 0);
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
        let (p_internal, p_external) = EventPair::create().unwrap();
        let (unblocker, unblocker_recv) = oneshot::channel();
        let (blobfs_verifier, blobfs_verifier_call_count) =
            success_blobfs_verifier_and_call_count();

        put_metadata_in_happy_state(
            &paver.spawn_boot_manager_service(),
            &p_internal,
            unblocker,
            &[&blobfs_verifier],
            &finspect::Node::default(),
            &Config::builder().build(),
        )
        .await
        .unwrap();

        assert_eq!(paver.take_events(), vec![PaverEvent::QueryCurrentConfiguration]);
        assert_eq!(
            p_external.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Ok(zx::Signals::USER_0)
        );
        assert_eq!(unblocker_recv.await, Ok(()));
        assert_eq!(blobfs_verifier_call_count.load(Ordering::SeqCst), 0);
    }

    /// When we're disabled, we should not update metadata.
    /// However, the FIDL server should still be unblocked.
    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_disabled() {
        // We shouldn't even attempt to talk to the paver when disabled, so a proxy with the remote
        // end closed should work fine.
        let boot_manager_proxy =
            create_proxy::<paver::BootManagerMarker>().expect("Creating proxy succeeds").0;
        let (p_internal, p_external) = EventPair::create().unwrap();
        let (unblocker, unblocker_recv) = oneshot::channel();
        let (blobfs_verifier, blobfs_verifier_call_count) =
            success_blobfs_verifier_and_call_count();

        put_metadata_in_happy_state(
            &boot_manager_proxy,
            &p_internal,
            unblocker,
            &[&blobfs_verifier],
            &finspect::Node::default(),
            &Config::builder().enable(false).build(),
        )
        .await
        .unwrap();

        assert_eq!(
            p_external.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Ok(zx::Signals::USER_0)
        );
        assert_eq!(unblocker_recv.await, Ok(()));
        assert_eq!(blobfs_verifier_call_count.load(Ordering::SeqCst), 0);
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
        let (p_internal, p_external) = EventPair::create().unwrap();
        let (unblocker, unblocker_recv) = oneshot::channel();
        let (blobfs_verifier, blobfs_verifier_call_count) =
            success_blobfs_verifier_and_call_count();

        put_metadata_in_happy_state(
            &paver.spawn_boot_manager_service(),
            &p_internal,
            unblocker,
            &[&blobfs_verifier],
            &finspect::Node::default(),
            &Config::builder().build(),
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() }
            ]
        );
        assert_eq!(
            p_external.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Ok(zx::Signals::USER_0)
        );
        assert_eq!(unblocker_recv.await, Ok(()));
        assert_eq!(blobfs_verifier_call_count.load(Ordering::SeqCst), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_current_is_healthy_a() {
        test_does_not_change_metadata_when_current_is_healthy(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_change_metadata_when_current_is_healthy_b() {
        test_does_not_change_metadata_when_current_is_healthy(&Configuration::B).await
    }

    /// When the current slot is pending, we should verify, commit, & unblock the fidl server.
    async fn test_verifies_and_commits_when_current_is_pending(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .build(),
        );
        let (p_internal, p_external) = EventPair::create().unwrap();
        let (unblocker, unblocker_recv) = oneshot::channel();
        let (blobfs_verifier, blobfs_verifier_call_count) =
            success_blobfs_verifier_and_call_count();

        put_metadata_in_happy_state(
            &paver.spawn_boot_manager_service(),
            &p_internal,
            unblocker,
            &[&blobfs_verifier],
            &finspect::Node::default(),
            &Config::builder().build(),
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
                PaverEvent::SetConfigurationHealthy { configuration: current_config.into() },
                PaverEvent::SetConfigurationUnbootable {
                    configuration: current_config.to_alternate().into()
                },
                PaverEvent::BootManagerFlush,
            ]
        );
        assert_eq!(OnSignals::new(&p_external, zx::Signals::USER_0).await, Ok(zx::Signals::USER_0));
        assert_eq!(unblocker_recv.await, Ok(()));
        assert_eq!(blobfs_verifier_call_count.load(Ordering::SeqCst), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_verifies_and_commits_when_current_is_pending_a() {
        test_verifies_and_commits_when_current_is_pending(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_verifies_and_commits_when_current_is_pending_b() {
        test_verifies_and_commits_when_current_is_pending(&Configuration::B).await
    }

    /// When we fail to verify and the config says to ignore, we should still do the commit.
    async fn test_commits_when_failed_verification_ignored(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .build(),
        );
        let (p_internal, p_external) = EventPair::create().unwrap();
        let (unblocker, unblocker_recv) = oneshot::channel();
        let (blobfs_verifier, blobfs_verifier_call_count) =
            failing_blobfs_verifier_and_call_count();

        put_metadata_in_happy_state(
            &paver.spawn_boot_manager_service(),
            &p_internal,
            unblocker,
            &[&blobfs_verifier],
            &finspect::Node::default(),
            &Config::builder().blobfs(Mode::Ignore).build(),
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
                PaverEvent::SetConfigurationHealthy { configuration: current_config.into() },
                PaverEvent::SetConfigurationUnbootable {
                    configuration: current_config.to_alternate().into()
                },
                PaverEvent::BootManagerFlush,
            ]
        );
        assert_eq!(OnSignals::new(&p_external, zx::Signals::USER_0).await, Ok(zx::Signals::USER_0));
        assert_eq!(unblocker_recv.await, Ok(()));
        assert_eq!(blobfs_verifier_call_count.load(Ordering::SeqCst), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_commits_when_failed_verification_ignored_a() {
        test_commits_when_failed_verification_ignored(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_commits_when_failed_verification_ignored_b() {
        test_commits_when_failed_verification_ignored(&Configuration::B).await
    }

    /// When we fail to verify and the config says to not ignore, we should report an error.
    async fn test_errors_when_failed_verification_not_ignored(current_config: &Configuration) {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .current_config(current_config.into())
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .build(),
        );
        let (p_internal, p_external) = EventPair::create().unwrap();
        let (unblocker, unblocker_recv) = oneshot::channel();
        let (blobfs_verifier, blobfs_verifier_call_count) =
            failing_blobfs_verifier_and_call_count();

        let res = put_metadata_in_happy_state(
            &paver.spawn_boot_manager_service(),
            &p_internal,
            unblocker,
            &[&blobfs_verifier],
            &finspect::Node::default(),
            &Config::builder().blobfs(Mode::RebootOnFailure).build(),
        )
        .await;

        let errors = assert_matches!(
            res,
            Err(MetadataError::Verify(VerifyErrors::VerifyErrors(s))) => s);
        assert_matches!(
            errors[..],
            [VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Verify(_))]
        );
        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::QueryCurrentConfiguration,
                PaverEvent::QueryConfigurationStatus { configuration: current_config.into() },
            ]
        );
        assert_eq!(
            p_external.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Err(zx::Status::TIMED_OUT)
        );
        assert_eq!(unblocker_recv.await, Ok(()));
        assert_eq!(blobfs_verifier_call_count.load(Ordering::SeqCst), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_errors_when_failed_verification_not_ignored_a() {
        test_errors_when_failed_verification_not_ignored(&Configuration::A).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_errors_when_failed_verification_not_ignored_b() {
        test_errors_when_failed_verification_not_ignored(&Configuration::B).await
    }
}
