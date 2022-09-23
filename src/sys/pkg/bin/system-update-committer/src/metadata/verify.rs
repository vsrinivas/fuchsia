// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        errors::{VerifyError, VerifyErrors, VerifyFailureReason, VerifySource},
        inspect::write_to_inspect,
    },
    ::fidl::client::QueryResponseFut,
    fidl_fuchsia_update_verify as fidl,
    fidl_fuchsia_update_verify::{VerifierVerifyResult, VerifyOptions},
    fuchsia_async::TimeoutExt as _,
    fuchsia_inspect as finspect,
    futures::future::{join_all, FutureExt as _, TryFutureExt as _},
    std::{
        future::Future,
        time::{Duration, Instant},
    },
};

// Each health verification should time out after 1 minute. This value should be at least 100X
// larger than the expected verification duration. When adding a new health verification, consider
// logging verification durations locally to validate this constant is still apropos.
const VERIFY_TIMEOUT: Duration = Duration::from_secs(60);

pub trait VerifierProxy {
    fn call_verify(&self, options: VerifyOptions) -> QueryResponseFut<VerifierVerifyResult>;
    fn source(&self) -> VerifySource;
}

impl VerifierProxy for fidl::BlobfsVerifierProxy {
    fn call_verify(&self, options: VerifyOptions) -> QueryResponseFut<VerifierVerifyResult> {
        self.verify(options)
    }
    fn source(&self) -> VerifySource {
        VerifySource::Blobfs
    }
}

/// Do the health verification and handle associated errors. This is NOT to be confused with
/// verified execution; health verification is a different process we use to determine if we should
/// give up on the backup slot.
pub fn do_health_verification<'a>(
    proxies: &'a [&dyn VerifierProxy],
    node: &'a finspect::Node,
) -> impl Future<Output = Result<(), VerifyErrors>> + 'a {
    let futures: Vec<_> = proxies
        .iter()
        .map(|proxy| async {
            let now = Instant::now();
            let res = proxy
                .call_verify(VerifyOptions::EMPTY)
                .map(|res| {
                    let res = res.map_err(VerifyFailureReason::Fidl)?;
                    res.map_err(VerifyFailureReason::Verify)
                })
                .on_timeout(VERIFY_TIMEOUT, || Err(VerifyFailureReason::Timeout))
                .map_err(|e| VerifyError::VerifyError(proxy.source(), e))
                .await;
            let () = write_to_inspect(node, &res, now.elapsed());
            res
        })
        .collect();

    async move {
        let res: Vec<VerifyError> =
            join_all(futures).await.into_iter().filter_map(|r| r.err()).collect();
        if res.is_empty() {
            Ok(())
        } else {
            Err(VerifyErrors::VerifyErrors(res))
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        futures::{future::BoxFuture, pin_mut, task::Poll},
        mock_verifier::{Hook, MockVerifierService},
        std::sync::Arc,
    };

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_pass() {
        let mock = Arc::new(MockVerifierService::new(|_| Ok(())));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();

        assert_matches!(
            do_health_verification(&[&proxy], &finspect::Node::default()).await,
            Ok(())
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_fail_verify() {
        let mock = Arc::new(MockVerifierService::new(|_| Err(fidl::VerifyError::Internal)));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();

        let errors = assert_matches!(
            do_health_verification(&[&proxy], &finspect::Node::default()).await,
            Err(VerifyErrors::VerifyErrors(s)) => s);
        assert_matches!(
            errors[..],
            [VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Verify(_))]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_one_succeeds_one_fails() {
        let mock_1 = Arc::new(MockVerifierService::new(|_| Err(fidl::VerifyError::Internal)));
        let (proxy_1, _server_1) = mock_1.spawn_blobfs_verifier_service();
        let mock_2 = Arc::new(MockVerifierService::new(|_| Ok(())));
        let (proxy_2, _server_2) = mock_2.spawn_blobfs_verifier_service();

        {
            let errors = assert_matches!(
              do_health_verification(&[&proxy_1, &proxy_2], &finspect::Node::default()).await,
              Err(VerifyErrors::VerifyErrors(s)) => s
            );
            assert_matches!(
                errors[..],
                [VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Verify(_))]
            );
        }

        {
            // The same, but in reverse order -- verify that the order of
            // proxies does not affect verification or filtering.
            let errors = assert_matches!(
            do_health_verification(&[&proxy_2, &proxy_1], &finspect::Node::default()).await,
            Err(VerifyErrors::VerifyErrors(s)) => s);
            assert_matches!(
                errors[..],
                [VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Verify(_))]
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_both_succeed() {
        let mock_1 = Arc::new(MockVerifierService::new(|_| Ok(())));
        let (proxy_1, _server_1) = mock_1.spawn_blobfs_verifier_service();
        let mock_2 = Arc::new(MockVerifierService::new(|_| Ok(())));
        let (proxy_2, _server_2) = mock_2.spawn_blobfs_verifier_service();

        assert_matches!(
            do_health_verification(&[&proxy_1, &proxy_2], &finspect::Node::default()).await,
            Ok(())
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_both_fail() {
        let mock_1 = Arc::new(MockVerifierService::new(|_| Err(fidl::VerifyError::Internal)));
        let (proxy_1, _server_1) = mock_1.spawn_blobfs_verifier_service();
        let mock_2 = Arc::new(MockVerifierService::new(|_| Err(fidl::VerifyError::Internal)));
        let (proxy_2, _server_2) = mock_2.spawn_blobfs_verifier_service();

        let errors = assert_matches!(
            do_health_verification(&[&proxy_1, &proxy_2], &finspect::Node::default()).await,
            Err(VerifyErrors::VerifyErrors(s)) => s
        );
        assert_matches!(
            errors[..],
            [
                VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Verify(_)),
                VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Verify(_)),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn blobfs_fail_fidl() {
        let mock = Arc::new(MockVerifierService::new(|_| Ok(())));
        let (proxy, server) = mock.spawn_blobfs_verifier_service();

        drop(server);

        let errors = assert_matches!(
            do_health_verification(&[&proxy], &finspect::Node::default()).await,
            Err(VerifyErrors::VerifyErrors(s)) => s);
        assert_matches!(
            errors[..],
            [VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Fidl(_))]
        );
    }

    /// Hook that will cause `verify` to never return.
    struct HangingVerifyHook;
    impl Hook for HangingVerifyHook {
        fn verify(&self, _options: VerifyOptions) -> BoxFuture<'static, VerifierVerifyResult> {
            futures::future::pending().boxed()
        }
    }

    #[test]
    fn blobfs_fail_timeout() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Create a mock blobfs verifier that will never respond.
        let mock = Arc::new(MockVerifierService::new(HangingVerifyHook));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();
        let node = finspect::Node::default();

        // Start do_health_verification, which will internally create the timeout future.
        let proxies: Vec<&dyn VerifierProxy> = vec![&proxy];
        let fut = do_health_verification(&proxies, &node);
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
                let errors = assert_matches!(
                    res,
                    Err(VerifyErrors::VerifyErrors(s)) => s);
                assert_matches!(
                    errors[..],
                    [VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Timeout)]
                );
            }
            Poll::Pending => panic!("future unexpectedly pending"),
        };
    }
}
