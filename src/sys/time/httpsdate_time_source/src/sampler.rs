// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::datatypes::HttpsSample;
use async_trait::async_trait;
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use httpdate_hyper::{HttpsDateError, NetworkTimeClient};
use hyper::Uri;

const HTTPS_TIMEOUT: zx::Duration = zx::Duration::from_seconds(10);
/// A coarse approximation of the standard deviation of a sample. The error is dominated by the
/// effect of the time quantization, so we neglect error from network latency entirely and return
/// a standard deviation of a uniform distribution 1 second wide.
// TODO(satsukiu): replace with a more accurate estimate
const STANDARD_DEVIATION: zx::Duration = zx::Duration::from_millis(289);
const HALF_SECOND: zx::Duration = zx::Duration::from_millis(500);
const ONE_SECOND: zx::Duration = zx::Duration::from_seconds(1);

#[async_trait]
/// An `HttpsDateClient` can make requests against a given uri to retrieve a UTC time.
pub trait HttpsDateClient {
    /// Poll |uri| once to obtain the current UTC time. The time is quantized to a second due to
    /// the format of the HTTP date header.
    async fn request_utc(&mut self, uri: &Uri) -> Result<zx::Time, HttpsDateError>;
}

#[async_trait]
impl HttpsDateClient for NetworkTimeClient {
    async fn request_utc(&mut self, uri: &Uri) -> Result<zx::Time, HttpsDateError> {
        let utc = self
            .get_network_time(uri.clone())
            .on_timeout(fasync::Time::after(HTTPS_TIMEOUT), || Err(HttpsDateError::NetworkError))
            .await?;
        Ok(zx::Time::from_nanos(utc.timestamp_nanos()))
    }
}

/// An `HttpsSampler` produces `HttpsSample`s by polling an HTTP server, possibly by combining
/// the results of multiple polls.
#[async_trait]
pub trait HttpsSampler {
    /// Produce a single `HttpsSample`.
    async fn produce_sample(&self) -> Result<HttpsSample, HttpsDateError>;
}

/// The default implementation of `HttpsSampler` that uses an `HttpsDateClient` to poll a server.
pub struct HttpsSamplerImpl<C: HttpsDateClient> {
    /// Client used to poll servers for time.
    client: Mutex<C>,
    /// URI called to obtain time.
    uri: Uri,
}

impl HttpsSamplerImpl<NetworkTimeClient> {
    /// Create a new `HttpsSamplerImpl` that makes requests against `uri` to poll time.
    pub fn new(uri: Uri) -> Self {
        Self::new_with_client(uri, NetworkTimeClient::new())
    }
}

impl<C: HttpsDateClient + Send> HttpsSamplerImpl<C> {
    fn new_with_client(uri: Uri, client: C) -> Self {
        Self { client: Mutex::new(client), uri }
    }
}

#[async_trait]
impl<C: HttpsDateClient + Send> HttpsSampler for HttpsSamplerImpl<C> {
    async fn produce_sample(&self) -> Result<HttpsSample, HttpsDateError> {
        let monotonic_before = zx::Time::get_monotonic();
        // We assume here that the time reported by an HTTP server is truncated down a second. We
        // provide the median value of the range of possible actual UTC times, which makes the
        // error distribution symmetric.
        let utc = self.client.lock().await.request_utc(&self.uri).await? + HALF_SECOND;
        let monotonic_after = zx::Time::get_monotonic();
        let monotonic_center = zx::Time::from_nanos(
            (monotonic_before.into_nanos() + monotonic_after.into_nanos()) / 2,
        );

        Ok(HttpsSample {
            utc,
            monotonic: monotonic_center,
            standard_deviation: STANDARD_DEVIATION,
            final_bound_size: monotonic_after - monotonic_before + ONE_SECOND,
            round_trip_times: vec![monotonic_after - monotonic_before],
        })
    }
}

#[cfg(test)]
pub use fake::FakeSampler;
#[cfg(test)]
mod fake {
    use super::*;
    use futures::{channel::oneshot, future::pending, Future};
    use std::{collections::VecDeque, iter::FromIterator};

    /// An |HttpsSampler| which responds with premade responses and signals when the responses
    /// have been given out.
    pub struct FakeSampler {
        /// Queue of responses.
        enqueued_responses: Mutex<VecDeque<Result<HttpsSample, HttpsDateError>>>,
        /// Channel used to signal exhaustion of the enqueued responses.
        completion_notifier: Mutex<Option<oneshot::Sender<()>>>,
    }

    #[async_trait]
    impl HttpsSampler for FakeSampler {
        async fn produce_sample(&self) -> Result<HttpsSample, HttpsDateError> {
            match self.enqueued_responses.lock().await.pop_front() {
                Some(result) => result,
                None => {
                    self.completion_notifier.lock().await.take().unwrap().send(()).unwrap();
                    pending().await
                }
            }
        }
    }

    impl FakeSampler {
        /// Create a test client and a future that resolves when all the contents
        /// of |responses| have been consumed.
        pub fn with_responses(
            responses: impl IntoIterator<Item = Result<HttpsSample, HttpsDateError>>,
        ) -> (Self, impl Future) {
            let (sender, receiver) = oneshot::channel();
            let client = FakeSampler {
                enqueued_responses: Mutex::new(VecDeque::from_iter(responses)),
                completion_notifier: Mutex::new(Some(sender)),
            };
            (client, receiver)
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::FutureExt;
    use lazy_static::lazy_static;
    use std::{collections::VecDeque, iter::FromIterator};

    lazy_static! {
        static ref TEST_URI: hyper::Uri = "https://localhost/".parse().unwrap();
        static ref TEST_UTC: zx::Time = zx::Time::from_nanos(1_000_000_000_000);
    }

    /// An |HttpsDateClient| which responds with premade responses.
    struct TestClient {
        /// Queue of responses.
        enqueued_responses: VecDeque<Result<zx::Time, HttpsDateError>>,
    }

    #[async_trait]
    impl HttpsDateClient for TestClient {
        async fn request_utc(&mut self, _uri: &Uri) -> Result<zx::Time, HttpsDateError> {
            self.enqueued_responses.pop_front().unwrap()
        }
    }

    impl TestClient {
        /// Create a test client that responds with the given |responses|.
        fn with_responses(
            responses: impl IntoIterator<Item = Result<zx::Time, HttpsDateError>>,
        ) -> Self {
            TestClient { enqueued_responses: VecDeque::from_iter(responses) }
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_produce_sample() {
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_responses(vec![Ok(*TEST_UTC)]),
        );
        let monotonic_before = zx::Time::get_monotonic();
        let sample = sampler.produce_sample().await.unwrap();
        let monotonic_after = zx::Time::get_monotonic();
        assert_eq!(sample.utc, *TEST_UTC + HALF_SECOND);
        assert!(sample.monotonic >= monotonic_before);
        assert!(sample.monotonic <= monotonic_after);
        assert_eq!(sample.standard_deviation, STANDARD_DEVIATION);
        assert!(sample.final_bound_size <= monotonic_after - monotonic_before + ONE_SECOND);
        assert_eq!(sample.round_trip_times.len(), 1);
        assert!(sample.round_trip_times[0] <= monotonic_after - monotonic_before);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_fake_sampler() {
        let expected_responses = vec![
            Ok(HttpsSample {
                utc: zx::Time::from_nanos(999),
                monotonic: zx::Time::from_nanos(888_888_888),
                standard_deviation: zx::Duration::from_nanos(22),
                final_bound_size: zx::Duration::from_nanos(44),
                round_trip_times: vec![zx::Duration::from_nanos(55)],
            }),
            Err(HttpsDateError::NetworkError),
            Err(HttpsDateError::NoCertificatesPresented),
        ];
        let (fake_sampler, complete_fut) = FakeSampler::with_responses(expected_responses.clone());
        for expected in expected_responses {
            assert_eq!(expected, fake_sampler.produce_sample().await);
        }

        // After exhausting canned responses, the sampler should stall.
        assert!(fake_sampler.produce_sample().now_or_never().is_none());
        // Completion is signalled via the future provided at construction.
        complete_fut.await;
    }
}
