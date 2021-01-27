// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        config::{Config, Mode},
        metadata::{MetadataError, VerifyError},
    },
    anyhow::{anyhow, Context},
    fidl_fuchsia_hardware_power_statecontrol::{
        AdminProxy as PowerStateControlProxy, RebootReason,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::Status,
};

/// Determines if we should reboot.
pub(super) fn should_reboot(error: &MetadataError, config: &Config) -> bool {
    if let MetadataError::Verify(VerifyError::BlobFs(_)) = error {
        return config.blobfs() == &Mode::RebootOnFailure;
    }
    return true;
}

/// Waits for the timer to complete and then reboots the system, logging errors instead of failing.
pub(super) async fn wait_and_reboot(proxy: &PowerStateControlProxy, timer: fasync::Timer) {
    let () = timer.await;
    if let Err(e) = async move {
        proxy
            .reboot(RebootReason::RetrySystemUpdate)
            .await
            .context("while performing reboot call")?
            .map_err(Status::from_raw)
            .context("reboot responded with")
    }
    .await
    {
        fx_log_err!("error initiating reboot: {:#}", anyhow!(e));
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::metadata::{BootManagerError, PolicyError, VerifyFailureReason},
        futures::{channel::oneshot, pin_mut, task::Poll},
        mock_reboot::MockRebootService,
        parking_lot::Mutex,
        proptest::prelude::*,
        std::{sync::Arc, time::Duration},
    };

    #[test]
    fn test_wait_and_reboot_success() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();

        // Create a mock reboot service.
        let (sender, recv) = oneshot::channel();
        let sender = Arc::new(Mutex::new(Some(sender)));
        let mock = Arc::new(MockRebootService::new(Box::new(move |reason: RebootReason| {
            sender.lock().take().unwrap().send(reason).unwrap();
            Ok(())
        })));
        let proxy = mock.spawn_reboot_service();

        // Prepare futures to call reboot and receive the reboot request.
        let timer_duration = 5;
        let reboot_fut =
            wait_and_reboot(&proxy, fasync::Timer::new(Duration::from_secs(timer_duration)));
        pin_mut!(reboot_fut);
        pin_mut!(recv);

        // Set the time so that the timer is still going, so we should neither call reboot nor
        // observe the reboot service was called.
        executor.set_fake_time(fasync::Time::after(Duration::from_secs(timer_duration - 1).into()));
        assert!(!executor.wake_expired_timers());
        match executor.run_until_stalled(&mut reboot_fut) {
            Poll::Ready(res) => panic!("future unexpectedly completed with response: {:?}", res),
            Poll::Pending => {}
        };
        match executor.run_until_stalled(&mut recv) {
            Poll::Ready(res) => panic!("future unexpectedly completed with response: {:?}", res),
            Poll::Pending => {}
        };

        // Once the timer completes, we should complete the reboot call and observe we called the
        // reboot service with the given reboot reason.
        executor.set_fake_time(fasync::Time::after(Duration::from_secs(1).into()));
        assert!(executor.wake_expired_timers());
        match executor.run_until_stalled(&mut recv) {
            Poll::Ready(res) => panic!("future unexpectedly completed with response: {:?}", res),
            Poll::Pending => {}
        };
        match executor.run_until_stalled(&mut reboot_fut) {
            Poll::Ready(_) => {}
            Poll::Pending => panic!("future unexpectedly pending"),
        };
        match executor.run_until_stalled(&mut recv) {
            Poll::Ready(res) => assert_eq!(res, Ok(RebootReason::RetrySystemUpdate)),
            Poll::Pending => panic!("future unexpectedly pending"),
        };
    }

    fn test_blobfs_verify_errors(config: Config, expect_reboot: bool) {
        let timeout_err = MetadataError::Verify(VerifyError::BlobFs(VerifyFailureReason::Timeout));
        let verify_err =
            MetadataError::Verify(VerifyError::BlobFs(VerifyFailureReason::Verify(anyhow!("foo"))));
        let fidl_err = MetadataError::Verify(VerifyError::BlobFs(VerifyFailureReason::Fidl(
            fidl::Error::OutOfRange,
        )));

        assert_eq!(should_reboot(&timeout_err, &config), expect_reboot);
        assert_eq!(should_reboot(&verify_err, &config), expect_reboot);
        assert_eq!(should_reboot(&fidl_err, &config), expect_reboot);
    }

    /// Blobfs errors SHOULD NOT cause a reboot if they're ignored.
    #[test]
    fn test_blobfs_errors_should_not_reboot() {
        test_blobfs_verify_errors(Config::builder().blobfs(Mode::Ignore).build(), false);
    }

    /// Blobfs errors SHOULD cause a reboot if they're NOT ignored.
    #[test]
    fn test_blobfs_errors_should_reboot() {
        test_blobfs_verify_errors(Config::builder().blobfs(Mode::RebootOnFailure).build(), true);
    }

    // Test that all the non-blobfs metadata errors will always cause a reboot, regardless of the
    // config. Ideally, we'd also generate arbitrary MetadataErrors, but that's not possible
    // because there are !Clone descendants.
    proptest! {
        #[test]
        fn test_should_reboot_commit_error(config: Config) {
            let err = MetadataError::Commit(BootManagerError::Status {
                method_name: "foo",
                status: Status::UNAVAILABLE,
            });
            assert!(should_reboot(&err, &config));
        }

        #[test]
        fn test_should_reboot_policy_error(config: Config) {
            let err = MetadataError::Policy(PolicyError::Build(BootManagerError::Status {
                method_name: "bar",
                status: Status::ACCESS_DENIED,
            }));
            assert!(should_reboot(&err, &config));
        }

        #[test]
        fn test_should_reboot_signal_error(config: Config) {
            let err = MetadataError::SignalPeer(fuchsia_zircon::Status::NOT_FOUND);
            assert!(should_reboot(&err, &config));
        }

        #[test]
        fn test_should_reboot_unblock_error(config: Config) {
            assert!(should_reboot(&MetadataError::Unblock, &config));
        }
    }
}
