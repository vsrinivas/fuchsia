// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::diagnostics::Diagnostics;
use crate::sampler::HttpsSampler;
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_time_external::{Properties, Status};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{channel::mpsc::Sender, SinkExt};
use httpdate_hyper::HttpsDateError;
use log::{error, info, warn};
use push_source::{Update, UpdateAlgorithm};

const POLLS_PER_SAMPLE: u32 = 5;

/// A definition of how long an algorithm should wait between polls. Defines a fixed wait duration
/// following successful poll attempts, and a capped exponential backoff following failed poll
/// attempts.
pub struct RetryStrategy {
    pub min_between_failures: zx::Duration,
    pub max_exponent: u32,
    pub tries_per_exponent: u32,
    pub between_successes: zx::Duration,
}

impl RetryStrategy {
    /// Returns the duration to wait after a failed poll attempt. |attempt_index| is a zero-based
    /// index of the failed attempt, i.e. after the third failed attempt `attempt_index` = 2.
    fn backoff_duration(&self, attempt_index: u32) -> zx::Duration {
        let exponent = std::cmp::min(attempt_index / self.tries_per_exponent, self.max_exponent);
        zx::Duration::from_nanos(self.min_between_failures.into_nanos() * 2i64.pow(exponent))
    }
}

/// An |UpdateAlgorithm| that uses an `HttpsSampler` to obtain time samples at a schedule
/// dictated by a specified retry strategy.
pub struct HttpsDateUpdateAlgorithm<S: HttpsSampler + Send + Sync, D: Diagnostics> {
    /// Strategy defining how long to wait after successes and failures.
    retry_strategy: RetryStrategy,
    /// Sampler used to produce samples.
    sampler: S,
    /// Object managing diagnostics output.
    diagnostics: D,
}

#[async_trait]
impl<S, D> UpdateAlgorithm for HttpsDateUpdateAlgorithm<S, D>
where
    S: HttpsSampler + Send + Sync,
    D: Diagnostics,
{
    async fn update_device_properties(&self, _properties: Properties) {
        // since our samples are polled independently for now, we don't need to use
        // device properties yet.
    }

    async fn generate_updates(&self, mut sink: Sender<Update>) -> Result<(), Error> {
        // TODO(fxbug.dev/59972): wait for network to be available before polling.
        loop {
            self.try_generate_sample_until_successful(&mut sink).await?;
            fasync::Timer::new(fasync::Time::after(self.retry_strategy.between_successes.clone()))
                .await;
        }
    }
}

impl<S, D> HttpsDateUpdateAlgorithm<S, D>
where
    S: HttpsSampler + Send + Sync,
    D: Diagnostics,
{
    pub fn new(retry_strategy: RetryStrategy, diagnostics: D, sampler: S) -> Self {
        Self { retry_strategy, sampler, diagnostics }
    }

    /// Repeatedly poll for a time until one sample is successfully retrieved. Pushes updates to
    /// |sink|.
    async fn try_generate_sample_until_successful(
        &self,
        sink: &mut Sender<Update>,
    ) -> Result<(), Error> {
        let mut attempt_iter = 0u32..;
        let mut last_error = None;
        loop {
            let attempt = attempt_iter.next().unwrap_or(u32::MAX);
            match self.sampler.produce_sample(POLLS_PER_SAMPLE).await {
                Ok(sample) => {
                    info!(
                        "Got a time sample - UTC {:?}, bound size {:?}, and round trip times {:?}",
                        sample.utc, sample.final_bound_size, sample.round_trip_times
                    );
                    self.diagnostics.success(&sample);
                    sink.send(Status::Ok.into()).await?;
                    sink.send(sample.into()).await?;
                    return Ok(());
                }
                Err(http_error) => {
                    self.diagnostics.failure(&http_error);
                    if Some(http_error) != last_error {
                        last_error = Some(http_error);
                        let status = match http_error {
                            HttpsDateError::InvalidHostname | HttpsDateError::SchemeNotHttps => {
                                // TODO(fxbug.dev/59771) - decide how to surface irrecoverable
                                // errors to clients
                                error!(
                                    "Got an unexpected error {:?}, which indicates a bad \
                                    configuration.",
                                    http_error
                                );
                                Status::UnknownUnhealthy
                            }
                            HttpsDateError::NetworkError => {
                                warn!("Failed to poll time: {:?}", http_error);
                                Status::Network
                            }
                            _ => {
                                warn!("Failed to poll time: {:?}", http_error);
                                Status::Protocol
                            }
                        };
                        sink.send(status.into()).await?;
                    }
                }
            }
            fasync::Timer::new(fasync::Time::after(self.retry_strategy.backoff_duration(attempt)))
                .await;
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::datatypes::HttpsSample;
    use crate::diagnostics::FakeDiagnostics;
    use crate::sampler::FakeSampler;
    use fidl_fuchsia_time_external::TimeSample;
    use futures::{channel::mpsc::channel, stream::StreamExt, task::Poll};
    use lazy_static::lazy_static;
    use matches::assert_matches;
    use std::sync::Arc;

    /// Test retry strategy with minimal wait periods.
    const TEST_RETRY_STRATEGY: RetryStrategy = RetryStrategy {
        min_between_failures: zx::Duration::from_nanos(100),
        max_exponent: 1,
        tries_per_exponent: 1,
        between_successes: zx::Duration::from_nanos(100),
    };

    lazy_static! {
        static ref TEST_SAMPLE_1: HttpsSample = HttpsSample {
            utc: zx::Time::from_nanos(111_222_333_444_555),
            monotonic: zx::Time::from_nanos(666_777_888_999_000),
            standard_deviation: zx::Duration::from_millis(101),
            final_bound_size: zx::Duration::from_millis(20),
            round_trip_times: vec![],
        };
        static ref TEST_SAMPLE_2: HttpsSample = HttpsSample {
            utc: zx::Time::from_nanos(999_999_999_999_999),
            monotonic: zx::Time::from_nanos(777_777_777_777_777),
            standard_deviation: zx::Duration::from_millis(102),
            final_bound_size: zx::Duration::from_millis(30),
            round_trip_times: vec![zx::Duration::from_millis(23)],
        };
    }

    fn to_fidl_time_sample(sample: &HttpsSample) -> Arc<TimeSample> {
        Arc::new(TimeSample {
            utc: Some(sample.utc.into_nanos()),
            monotonic: Some(sample.monotonic.into_nanos()),
            standard_deviation: Some(sample.standard_deviation.into_nanos()),
        })
    }

    #[test]
    fn test_retry_strategy() {
        let strategy = RetryStrategy {
            min_between_failures: zx::Duration::from_seconds(1),
            max_exponent: 3,
            tries_per_exponent: 3,
            between_successes: zx::Duration::from_seconds(60),
        };
        let expectation = vec![1, 1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 8, 8];
        for i in 0..expectation.len() {
            let expected = zx::Duration::from_seconds(expectation[i]);
            let actual = strategy.backoff_duration(i as u32);

            assert_eq!(
                actual, expected,
                "backoff after iteration {} should be {:?} but was {:?}",
                i, expected, actual
            );
        }
    }

    #[test]
    fn test_update_task_blocks_until_update_processed() {
        // Tests that our update loop blocks execution when run using a channel with zero capacity
        // as is done from PushSource. This verifies that each update is processed before another
        // is produced.
        // TODO(satsukiu) - use a generator instead and remove this test.
        let mut executor = fasync::Executor::new().unwrap();

        let (sampler, _response_complete_fut) =
            FakeSampler::with_responses(vec![Ok(TEST_SAMPLE_1.clone()), Ok(TEST_SAMPLE_2.clone())]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let update_algorithm =
            HttpsDateUpdateAlgorithm::new(TEST_RETRY_STRATEGY, diagnostics, sampler);
        let (sender, mut receiver) = channel(0);
        let mut update_fut = update_algorithm.generate_updates(sender);

        // After running to a stall, only the first update is available
        assert!(executor.run_until_stalled(&mut update_fut).is_pending());
        assert_eq!(
            executor.run_until_stalled(&mut receiver.next()),
            Poll::Ready(Some(Status::Ok.into()))
        );
        assert!(executor.run_until_stalled(&mut receiver.next()).is_pending());

        // Running the update task again to a stall produces a second update.
        assert!(executor.run_until_stalled(&mut update_fut).is_pending());
        assert_matches!(
            executor.run_until_stalled(&mut receiver.next()),
            Poll::Ready(Some(Update::Sample(_)))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_successful_updates() {
        let expected_samples = vec![TEST_SAMPLE_1.clone(), TEST_SAMPLE_2.clone()];
        let (sampler, response_complete_fut) =
            FakeSampler::with_responses(expected_samples.iter().map(|sample| Ok(sample.clone())));
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let update_algorithm =
            HttpsDateUpdateAlgorithm::new(TEST_RETRY_STRATEGY, Arc::clone(&diagnostics), sampler);

        let (sender, receiver) = channel(0);
        let _update_task =
            fasync::Task::spawn(async move { update_algorithm.generate_updates(sender).await });
        let updates = receiver.take_until(response_complete_fut).collect::<Vec<_>>().await;

        // The first update should indicate status OK, and any subsequent status updates should
        // indicate OK.
        assert_eq!(updates.first().unwrap(), &Status::Ok.into());
        assert!(updates
            .iter()
            .filter(|update| update.is_status())
            .all(|update| update == &Status::Ok.into()));

        let samples = updates
            .iter()
            .filter_map(|update| match update {
                Update::Sample(s) => Some(Arc::clone(s)),
                Update::Status(_) => None,
            })
            .collect::<Vec<_>>();
        assert_eq!(samples, expected_samples.iter().map(to_fidl_time_sample).collect::<Vec<_>>());
        assert_eq!(diagnostics.successes(), expected_samples);
        assert!(diagnostics.failures().is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_retry_until_successful() {
        let injected_responses = vec![
            Err(HttpsDateError::NetworkError),
            Err(HttpsDateError::NetworkError),
            Err(HttpsDateError::NoCertificatesPresented),
            Ok(TEST_SAMPLE_1.clone()),
        ];
        let (sampler, response_complete_fut) = FakeSampler::with_responses(injected_responses);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let update_algorithm =
            HttpsDateUpdateAlgorithm::new(TEST_RETRY_STRATEGY, Arc::clone(&diagnostics), sampler);

        let (sender, receiver) = channel(0);
        let _update_task =
            fasync::Task::spawn(async move { update_algorithm.generate_updates(sender).await });
        let updates = receiver.take_until(response_complete_fut).collect::<Vec<_>>().await;

        // Each status should be reported.
        let expected_status_updates: Vec<Update> =
            vec![Status::Network.into(), Status::Protocol.into(), Status::Ok.into()];
        let received_status_updates =
            updates.iter().filter(|updates| updates.is_status()).cloned().collect::<Vec<_>>();
        assert_eq!(expected_status_updates, received_status_updates);

        // Last update should be the new sample.
        let last_update = updates.iter().last().unwrap();
        match last_update {
            Update::Sample(sample) => assert_eq!(*sample, to_fidl_time_sample(&*TEST_SAMPLE_1)),
            Update::Status(_) => panic!("Expected a sample but got an update"),
        }

        assert_eq!(diagnostics.successes(), vec![TEST_SAMPLE_1.clone()]);
        assert_eq!(
            diagnostics.failures(),
            vec![
                HttpsDateError::NetworkError,
                HttpsDateError::NetworkError,
                HttpsDateError::NoCertificatesPresented
            ]
        );
    }
}
