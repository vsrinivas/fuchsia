// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_time_external::{Properties, Status, TimeSample};
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_zircon as zx;
use futures::{channel::mpsc::Sender, lock::Mutex, SinkExt, TryFutureExt};
use httpdate_hyper::{HttpsDateError, NetworkTimeClient};
use hyper::Uri;
use log::{error, info, warn};
use push_source::{Update, UpdateAlgorithm};

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

const HTTPS_TIMEOUT: zx::Duration = zx::Duration::from_seconds(10);

#[async_trait]
/// An `HttpsDateClient` can make requests against a given uri to retrieve a UTC time.
pub trait HttpsDateClient {
    /// Obtain the current UTC time and return it as nanoseconds since the epoch.
    async fn request_utc(&mut self, uri: &Uri) -> Result<i64, Status>;
}

#[async_trait]
impl HttpsDateClient for NetworkTimeClient {
    async fn request_utc(&mut self, uri: &Uri) -> Result<i64, Status> {
        let utc = self
            .get_network_time(uri.clone())
            .map_err(|e| match e {
                HttpsDateError::InvalidHostname | HttpsDateError::SchemeNotHttps => {
                    // TODO(fxbug.dev/59771) - decide how to surface irrecoverable errors to clients
                    error!("Got an unexpected error {:?}, which indicates a bad configuration.", e);
                    Status::UnknownUnhealthy
                }
                HttpsDateError::NetworkError => {
                    warn!("Failed to poll time: {:?}", e);
                    Status::Network
                }
                err => {
                    warn!("Failed to poll time: {:?}", err);
                    Status::Protocol
                }
            })
            .on_timeout(fasync::Time::after(HTTPS_TIMEOUT), || {
                warn!("Failed to poll time due to timeout");
                Err(Status::Network)
            })
            .await?;
        Ok(utc.timestamp_nanos())
    }
}

/// An |UpdateAlgorithm| that retrieves UTC time by pulling dates off of HTTP responses.
pub struct HttpsDateUpdateAlgorithm<C: HttpsDateClient + Send> {
    /// Strategy defining how long to wait after successes and failures.
    retry_strategy: RetryStrategy,
    /// Uri requested to obtain time.
    request_uri: Uri,
    /// Client used to make requests.
    client: Mutex<C>,
}

#[async_trait]
impl<C: HttpsDateClient + Send> UpdateAlgorithm for HttpsDateUpdateAlgorithm<C> {
    async fn update_device_properties(&self, _properties: Properties) {
        // since our samples are polled independently for now, we don't need to use
        // device properties yet.
    }

    async fn generate_updates(&self, mut sink: Sender<Update>) -> Result<(), Error> {
        // TODO(fxbug.dev/59972): wait for network to be available before polling.
        loop {
            self.poll_time_until_successful(&mut sink).await?;
            fasync::Timer::new(fasync::Time::after(self.retry_strategy.between_successes.clone()))
                .await;
        }
    }
}

impl HttpsDateUpdateAlgorithm<NetworkTimeClient> {
    /// Create a new |HttpsDateUpdateAlgorithm|.
    pub fn new(retry_strategy: RetryStrategy, request_uri: Uri) -> Self {
        Self::with_client(retry_strategy, request_uri, NetworkTimeClient::new())
    }
}

impl<C: HttpsDateClient + Send> HttpsDateUpdateAlgorithm<C> {
    fn with_client(retry_strategy: RetryStrategy, request_uri: Uri, client: C) -> Self {
        Self { retry_strategy, request_uri, client: Mutex::new(client) }
    }

    /// Repeatedly poll for a time until one sample is successfully retrieved. Pushes updates to
    /// |sink|.
    async fn poll_time_until_successful(&self, sink: &mut Sender<Update>) -> Result<(), Error> {
        info!("Attempting to poll time");
        let mut attempt_iter = 0u32..;
        loop {
            let attempt = attempt_iter.next().unwrap_or(u32::MAX);
            match self.poll_time_once().await {
                Ok(sample) => {
                    sink.send(Status::Ok.into()).await?;
                    sink.send(sample.into()).await?;
                    return Ok(());
                }
                Err(status) => sink.send(status.into()).await?,
            }
            fasync::Timer::new(fasync::Time::after(self.retry_strategy.backoff_duration(attempt)))
                .await;
        }
    }

    /// Poll for time once without retries.
    async fn poll_time_once(&self) -> Result<TimeSample, Status> {
        let mut client_lock = self.client.lock().await;

        let monotonic_before = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        let utc = client_lock.request_utc(&self.request_uri).await?;
        let monotonic_after = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        let monotonic_center = (monotonic_before + monotonic_after) / 2;

        Ok(TimeSample {
            utc: Some(utc),
            monotonic: Some(monotonic_center),
            standard_deviation: None,
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::{
        channel::{mpsc::channel, oneshot},
        future::pending,
        stream::StreamExt,
        task::Poll,
        Future,
    };
    use lazy_static::lazy_static;
    use matches::assert_matches;
    use std::{collections::VecDeque, iter::FromIterator};

    /// Test retry strategy with minimal wait periods.
    const TEST_RETRY_STRATEGY: RetryStrategy = RetryStrategy {
        min_between_failures: zx::Duration::from_nanos(100),
        max_exponent: 1,
        tries_per_exponent: 1,
        between_successes: zx::Duration::from_nanos(100),
    };

    lazy_static! {
        static ref TEST_URI: hyper::Uri = "https://localhost/".parse().unwrap();
    }

    /// An |HttpsDateClient| which responds with premade responses and signals when the responses
    /// have been given out.
    struct TestClient {
        /// Queue of responses.
        enqueued_responses: VecDeque<Result<i64, Status>>,
        /// Channel used to signal exhaustion of the enqueued responses.
        completion_notifier: Option<oneshot::Sender<()>>,
    }

    #[async_trait]
    impl HttpsDateClient for TestClient {
        async fn request_utc(&mut self, _uri: &Uri) -> Result<i64, Status> {
            match self.enqueued_responses.pop_front() {
                Some(result) => result,
                None => {
                    self.completion_notifier.take().unwrap().send(()).unwrap();
                    pending().await
                }
            }
        }
    }

    impl TestClient {
        /// Create a test client and a future that resolves when all the contents
        /// of |responses| have been consumed.
        fn with_responses(
            responses: impl IntoIterator<Item = Result<i64, Status>>,
        ) -> (Self, impl Future) {
            let (sender, receiver) = oneshot::channel();
            let client = TestClient {
                enqueued_responses: VecDeque::from_iter(responses),
                completion_notifier: Some(sender),
            };
            (client, receiver)
        }
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

        let (client, _response_complete_fut) = TestClient::with_responses(vec![Ok(2030)]);
        let update_algorithm =
            HttpsDateUpdateAlgorithm::with_client(TEST_RETRY_STRATEGY, TEST_URI.clone(), client);
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
        let expected_utc_times: Vec<i64> = vec![2020, 2021, 2022, 2023];
        let (client, response_complete_fut) =
            TestClient::with_responses(expected_utc_times.iter().map(|utc_nanos| Ok(*utc_nanos)));
        let update_algorithm =
            HttpsDateUpdateAlgorithm::with_client(TEST_RETRY_STRATEGY, TEST_URI.clone(), client);

        let monotonic_before = zx::Time::get(zx::ClockId::Monotonic).into_nanos();

        let (sender, receiver) = channel(0);
        let _update_task =
            fasync::Task::spawn(async move { update_algorithm.generate_updates(sender).await });
        let updates = receiver.take_until(response_complete_fut).collect::<Vec<_>>().await;

        let monotonic_after = zx::Time::get(zx::ClockId::Monotonic).into_nanos();

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
                Update::Sample(s) => Some(s),
                Update::Status(_) => None,
            })
            .collect::<Vec<_>>();
        assert_eq!(samples.len(), expected_utc_times.len());
        for (expected_utc_time, sample) in expected_utc_times.iter().zip(samples) {
            assert_eq!(*expected_utc_time, sample.utc.unwrap());
            assert!(sample.monotonic.unwrap() >= monotonic_before);
            assert!(sample.monotonic.unwrap() <= monotonic_after);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_retry_until_successful() {
        let sample_utc = 2125;
        let injected_responses =
            vec![Err(Status::Network), Err(Status::Network), Err(Status::Resource), Ok(sample_utc)];
        let (client, response_complete_fut) = TestClient::with_responses(injected_responses);
        let update_algorithm =
            HttpsDateUpdateAlgorithm::with_client(TEST_RETRY_STRATEGY, TEST_URI.clone(), client);

        let (sender, receiver) = channel(0);
        let _update_task =
            fasync::Task::spawn(async move { update_algorithm.generate_updates(sender).await });
        let updates = receiver.take_until(response_complete_fut).collect::<Vec<_>>().await;

        // Each status should be reported.
        let expected_status_updates: Vec<Update> = vec![
            Status::Network.into(),
            Status::Network.into(),
            Status::Resource.into(),
            Status::Ok.into(),
        ];
        let received_status_updates =
            updates.iter().filter(|updates| updates.is_status()).cloned().collect::<Vec<_>>();
        assert_eq!(expected_status_updates, received_status_updates);

        // Last update should be the new sample.
        let last_update = updates.iter().last().unwrap();
        match last_update {
            Update::Sample(sample) => assert_eq!(sample.utc.unwrap(), sample_utc),
            Update::Status(_) => panic!("Expected a sample but got an update"),
        }
    }
}
