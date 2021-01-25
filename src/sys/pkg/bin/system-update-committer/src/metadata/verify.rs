// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::errors::{VerifyError, VerifyFailureReason},
    anyhow::anyhow,
    fidl_fuchsia_update_verify::{BlobfsVerifierProxy, VerifyOptions},
    fuchsia_async as fasync,
    futures::prelude::*,
    std::time::Duration,
};

/// Dummy function to indicate where health verification will eventually go, and how to handle
/// associated errors. This is NOT to be confused with verified execution; health verification
/// is a different process we use to determine if we should give up on the backup slot.
pub async fn do_health_verification() -> Result<(), VerifyError> {
    Ok(())
}

// Each health verification should time out after 1 hour.
const VERIFY_TIMEOUT: Duration = Duration::from_secs(60 * 60);

// TODO(fxbug.dev/67381) call this from do_health_verification.
#[allow(dead_code)]
fn do_health_verification_impl(
    proxy: &BlobfsVerifierProxy,
) -> impl Future<Output = Result<(), VerifyError>> {
    let mut timer_fut = fasync::Timer::new(VERIFY_TIMEOUT).fuse();
    let mut verify_fut = proxy.verify(VerifyOptions { ..VerifyOptions::EMPTY }).fuse();

    // Report the blobfs verify result. If we add more verifications, we can factor each
    // verification into its own function and run all of them asynchronously.
    async move {
        futures::select! {
            verify_res = verify_fut => verify_res
                .map_err(|e| VerifyError::BlobFs(VerifyFailureReason::Fidl(e)))?
                .map_err(|e| VerifyError::BlobFs(VerifyFailureReason::Verify(anyhow!("{:?}", e)))),
            _ = timer_fut => Err(VerifyError::BlobFs(VerifyFailureReason::Timeout)),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_update_verify as fidl,
        futures::{future::BoxFuture, pin_mut, task::Poll},
        matches::assert_matches,
        mock_verifier::{Hook, MockVerifierService},
        std::sync::Arc,
    };

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_pass() {
        let mock = Arc::new(MockVerifierService::new(|_| Ok(())));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();

        assert_matches!(do_health_verification_impl(&proxy).await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_fail_verify() {
        let mock = Arc::new(MockVerifierService::new(|_| Err(fidl::VerifyError::Internal)));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();

        assert_matches!(
            do_health_verification_impl(&proxy).await,
            Err(VerifyError::BlobFs(VerifyFailureReason::Verify(_)))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_fail_fidl() {
        let mock = Arc::new(MockVerifierService::new(|_| Ok(())));
        let (proxy, server) = mock.spawn_blobfs_verifier_service();

        drop(server);

        assert_matches!(
            do_health_verification_impl(&proxy).await,
            Err(VerifyError::BlobFs(VerifyFailureReason::Fidl(_)))
        );
    }

    /// Hook that will cause `verify` to never return.
    struct HangingVerifyHook;
    impl Hook for HangingVerifyHook {
        fn verify(
            &self,
            _options: fidl::VerifyOptions,
        ) -> BoxFuture<'static, fidl::VerifierVerifyResult> {
            futures::future::pending().boxed()
        }
    }

    #[test]
    fn blobfs_fail_timeout() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();

        // Create a mock blobfs verifier that will never respond.
        let mock = Arc::new(MockVerifierService::new(HangingVerifyHook));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();

        // Start do_health_verification, which will internally create the timeout future.
        let fut = do_health_verification_impl(&proxy);
        pin_mut!(fut);

        // Since the timer has not expired, the future should still be pending.
        match executor.run_until_stalled(&mut fut) {
            Poll::Ready(res) => panic!("future unexpectedly completed with response: {:?}", res),
            Poll::Pending => {}
        };

        // Set the time so that the verify timeout expires.
        executor
            .set_fake_time(fasync::Time::after((VERIFY_TIMEOUT + Duration::from_secs(1)).into()));
        assert!(executor.wake_expired_timers());

        // Verify we get the Timeout error.
        match executor.run_until_stalled(&mut fut) {
            Poll::Ready(res) => {
                assert_matches!(res, Err(VerifyError::BlobFs(VerifyFailureReason::Timeout)))
            }
            Poll::Pending => panic!("future unexpectedly pending"),
        };
    }
}
