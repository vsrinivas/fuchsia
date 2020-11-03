// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bound::Bound;
use crate::datatypes::HttpsSample;
use async_trait::async_trait;
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use httpdate_hyper::{HttpsDateError, NetworkTimeClient};
use hyper::Uri;
use log::warn;

const HTTPS_TIMEOUT: zx::Duration = zx::Duration::from_seconds(10);
const NANOS_IN_SECONDS: i64 = 1_000_000_000;
/// The ratio between a standard deviation and a final bound size, expressed as a percentage.
/// This value is based on a small experiment to estimate the spread of errors across various
/// bound sizes.
// TODO(63370): consider round trip times when calculating standard deviation.
const STANDARD_DEVIATION_BOUND_PERCENTAGE: i64 = 30;
/// A guess as to how many times longer than a subsequent poll the first poll will take. This
/// encapsulates the additional time required during the first HTTPS request to setup a TLS
/// connection and is used to make a best guess for how long the second call will take.
const FIRST_RTT_TIME_FACTOR: i64 = 5;

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
    /// Produce a single `HttpsSample` by polling |num_polls| times.
    async fn produce_sample(&self, num_polls: u32) -> Result<HttpsSample, HttpsDateError>;
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
    async fn produce_sample(&self, num_polls: u32) -> Result<HttpsSample, HttpsDateError> {
        let (mut bound, first_rtt) = self.poll_server().await?;
        let mut round_trip_times = vec![first_rtt];

        for _ in 1..num_polls {
            let ideal_next_poll_time = ideal_next_poll_time(&bound, &round_trip_times);
            fasync::Timer::new(ideal_next_poll_time).await;

            // For subsequent polls ignore errors. This allows producing a degraded sample
            // instead of outright failing as long as one poll succeeds.
            if let Ok((new_bound, new_rtt)) = self.poll_server().await {
                bound = match bound.combine(&new_bound) {
                    Some(combined) => combined,
                    None => {
                        // Bounds might fail to combine if e.g. the device went to sleep and
                        // monotonic time was not updated. We assume the most recent poll is most
                        // accurate and discard accumulated information.
                        // TODO(satsukiu): report this event to Cobalt
                        round_trip_times.clear();
                        warn!("Unable to combine time bound, time may have moved.");
                        new_bound
                    }
                };
                round_trip_times.push(new_rtt);
            }
        }

        Ok(HttpsSample {
            utc: avg_time(bound.utc_min, bound.utc_max),
            monotonic: bound.monotonic,
            standard_deviation: bound.size() * STANDARD_DEVIATION_BOUND_PERCENTAGE / 100,
            final_bound_size: bound.size(),
            round_trip_times,
        })
    }
}

impl<C: HttpsDateClient + Send> HttpsSamplerImpl<C> {
    /// Poll the server once to produce a fresh bound on the UTC time. Returns a bound and the
    /// observed round trip time.
    async fn poll_server(&self) -> Result<(Bound, zx::Duration), HttpsDateError> {
        let monotonic_before = zx::Time::get_monotonic();
        let reported_utc = self.client.lock().await.request_utc(&self.uri).await?;
        let monotonic_after = zx::Time::get_monotonic();
        let round_trip_time = monotonic_after - monotonic_before;
        let monotonic_center = monotonic_before + round_trip_time / 2;
        // We assume here that the time reported by an HTTP server is truncated down to the second.
        // Thus the actual time on the server is in the range [reported_utc, reported_utc + 1).
        // Network latency adds additional uncertainty and is accounted for by expanding the bound
        // in either direction by half the observed round trip time.
        let bound = Bound {
            monotonic: monotonic_center,
            utc_min: reported_utc - round_trip_time / 2,
            utc_max: reported_utc + zx::Duration::from_seconds(1) + round_trip_time / 2,
        };
        Ok((bound, round_trip_time))
    }
}

/// Given a bound and observed round trip times, estimates the ideal monotonic time at which
/// to poll the server.
fn ideal_next_poll_time(bound: &Bound, observed_rtt: &[zx::Duration]) -> zx::Time {
    // Estimate the ideal monotonic time we'd like the server to check time.
    // ideal_server_check_time is a monotonic time at which bound's projection is centered
    // around a whole number of UTC seconds (utc_min = n - k, utc_max = n + k) where n is a
    // whole number of seconds.
    // Ignoring network latency, the bound produced by polling the server at
    // ideal_server_check_time must be [n-1, n) or [n, n + 1). In either case combining with
    // the original bound results in a bound half the size of the original.
    let ideal_server_check_time = bound.monotonic + zx::Duration::from_seconds(1)
        - time_subs(avg_time(bound.utc_min, bound.utc_max));

    // Since there is actually network latency, try to guess what it'll be and start polling
    // early so the server checks at the ideal time. The first poll takes longer than subsequent
    // polls due to TLS handshaking, so we make a best effort to account for that when the first
    // poll is the only one available. Otherwise, we discard the first poll rtt.
    let rtt_guess = match observed_rtt.len() {
        1 => observed_rtt[0] / FIRST_RTT_TIME_FACTOR,
        _ => avg(&observed_rtt[1..]),
    };
    let ideal_poll_start_time = ideal_server_check_time - rtt_guess / 2;

    // Adjust the time in case it has already passed.
    let now = zx::Time::get_monotonic();
    if now < ideal_poll_start_time {
        ideal_poll_start_time
    } else {
        ideal_poll_start_time + zx::Duration::from_seconds(1) + seconds(now - ideal_poll_start_time)
    }
}

/// Calculates the average of a set of small zx::Durations. May overflow for large durations.
fn avg(durations: &[zx::Duration]) -> zx::Duration {
    zx::Duration::from_nanos(
        durations.iter().map(|d| d.into_nanos()).sum::<i64>() / durations.len() as i64,
    )
}

/// Returns the whole second component of a zx::Duration.
fn seconds(duration: zx::Duration) -> zx::Duration {
    duration - subs(duration)
}

/// Returns the subsecond component of a zx::Duration.
fn subs(duration: zx::Duration) -> zx::Duration {
    zx::Duration::from_nanos(duration.into_nanos() % NANOS_IN_SECONDS)
}

/// Returns the midpoint of two zx::Times.
fn avg_time(time_1: zx::Time, time_2: zx::Time) -> zx::Time {
    zx::Time::from_nanos((time_1.into_nanos() + time_2.into_nanos()) / 2)
}

/// Returns the subsecond component of a zx::Time.
fn time_subs(time: zx::Time) -> zx::Duration {
    zx::Duration::from_nanos(time.into_nanos() % NANOS_IN_SECONDS)
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
        async fn produce_sample(&self, _num_polls: u32) -> Result<HttpsSample, HttpsDateError> {
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

    const ONE_SECOND: zx::Duration = zx::Duration::from_seconds(1);
    const RTT_TIMES_ZERO_LATENCY: [zx::Duration; 2] =
        [zx::Duration::from_nanos(0), zx::Duration::from_nanos(0)];
    const RTT_TIMES_100_MS_LATENCY: [zx::Duration; 4] = [
        zx::Duration::from_millis(500), // ignored
        zx::Duration::from_millis(50),
        zx::Duration::from_millis(100),
        zx::Duration::from_millis(150),
    ];
    const DURATION_50_MS: zx::Duration = zx::Duration::from_millis(50);

    const TEST_UTC_OFFSET: zx::Duration = zx::Duration::from_hours(72);

    /// An |HttpsDateClient| which responds with fake (quantized) UTC times at specified offsets
    /// from the monotonic time.
    struct TestClient {
        enqueued_offsets: VecDeque<Result<zx::Duration, HttpsDateError>>,
    }

    #[async_trait]
    impl HttpsDateClient for TestClient {
        async fn request_utc(&mut self, _uri: &Uri) -> Result<zx::Time, HttpsDateError> {
            let offset = self.enqueued_offsets.pop_front().unwrap()?;
            let unquantized_time = zx::Time::get_monotonic() + offset;
            Ok(unquantized_time - time_subs(unquantized_time))
        }
    }

    impl TestClient {
        /// Create a test client that calculates responses with the provided offsets.
        fn with_offset_responses(
            offsets: impl IntoIterator<Item = Result<zx::Duration, HttpsDateError>>,
        ) -> Self {
            TestClient { enqueued_offsets: VecDeque::from_iter(offsets) }
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_produce_sample_one_poll() {
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![Ok(TEST_UTC_OFFSET)]),
        );
        let monotonic_before = zx::Time::get_monotonic();
        let sample = sampler.produce_sample(1).await.unwrap();
        let monotonic_after = zx::Time::get_monotonic();

        assert!(sample.utc >= monotonic_before + TEST_UTC_OFFSET - ONE_SECOND);
        assert!(sample.utc <= monotonic_after + TEST_UTC_OFFSET + ONE_SECOND);

        assert!(sample.monotonic >= monotonic_before);
        assert!(sample.monotonic <= monotonic_after);
        assert_eq!(
            sample.standard_deviation,
            sample.final_bound_size * STANDARD_DEVIATION_BOUND_PERCENTAGE / 100
        );
        assert!(sample.final_bound_size <= monotonic_after - monotonic_before + ONE_SECOND);
        assert_eq!(sample.round_trip_times.len(), 1);
        assert!(sample.round_trip_times[0] <= monotonic_after - monotonic_before);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_produce_sample_multiple_polls() {
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
            ]),
        );
        let monotonic_before = zx::Time::get_monotonic();
        let sample = sampler.produce_sample(3).await.unwrap();
        let monotonic_after = zx::Time::get_monotonic();

        assert!(sample.utc >= monotonic_before + TEST_UTC_OFFSET - ONE_SECOND);
        assert!(sample.utc <= monotonic_after + TEST_UTC_OFFSET + ONE_SECOND);

        assert!(sample.monotonic >= monotonic_before);
        assert!(sample.monotonic <= monotonic_after);
        assert_eq!(
            sample.standard_deviation,
            sample.final_bound_size * STANDARD_DEVIATION_BOUND_PERCENTAGE / 100
        );
        assert!(sample.final_bound_size <= monotonic_after - monotonic_before + ONE_SECOND);
        assert_eq!(sample.round_trip_times.len(), 3);
        assert!(sample
            .round_trip_times
            .iter()
            .all(|rtt| *rtt <= monotonic_after - monotonic_before));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_produce_sample_fails_if_initial_poll_fails() {
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![Err(HttpsDateError::NetworkError)]),
        );

        assert_eq!(sampler.produce_sample(3).await.unwrap_err(), HttpsDateError::NetworkError);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_produce_sample_succeeds_if_subsequent_poll_fails() {
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
                Err(HttpsDateError::NetworkError),
            ]),
        );

        let sample = sampler.produce_sample(3).await.unwrap();
        assert_eq!(sample.round_trip_times.len(), 2);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_produce_sample_takes_later_poll_if_polls_disagree() {
        let expected_offset = TEST_UTC_OFFSET + zx::Duration::from_hours(1);
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
                Ok(expected_offset),
            ]),
        );

        let monotonic_before = zx::Time::get_monotonic();
        let sample = sampler.produce_sample(3).await.unwrap();
        let monotonic_after = zx::Time::get_monotonic();

        assert_eq!(sample.round_trip_times.len(), 1);
        assert!(sample.utc >= monotonic_before + expected_offset - ONE_SECOND);
        assert!(sample.utc <= monotonic_after + expected_offset + ONE_SECOND);
    }

    #[test]
    fn test_ideal_poll_time_in_future() {
        let future_monotonic = zx::Time::get_monotonic() + zx::Duration::from_hours(100);
        let bound_1 = Bound {
            monotonic: future_monotonic,
            utc_min: zx::Time::from_nanos(3_000_000_000),
            utc_max: zx::Time::from_nanos(4_000_000_000),
        };
        assert_eq!(
            ideal_next_poll_time(&bound_1, &RTT_TIMES_ZERO_LATENCY),
            future_monotonic + zx::Duration::from_millis(500),
        );
        assert_eq!(
            ideal_next_poll_time(&bound_1, &RTT_TIMES_100_MS_LATENCY),
            future_monotonic + zx::Duration::from_millis(500) - DURATION_50_MS,
        );

        let bound_2 = Bound {
            monotonic: future_monotonic,
            utc_min: zx::Time::from_nanos(3_600_000_000),
            utc_max: zx::Time::from_nanos(3_800_000_000),
        };
        assert_eq!(
            ideal_next_poll_time(&bound_2, &RTT_TIMES_ZERO_LATENCY),
            future_monotonic + zx::Duration::from_millis(300),
        );
        assert_eq!(
            ideal_next_poll_time(&bound_2, &RTT_TIMES_100_MS_LATENCY),
            future_monotonic + zx::Duration::from_millis(300) - DURATION_50_MS,
        );

        let bound_3 = Bound {
            monotonic: future_monotonic,
            utc_min: zx::Time::from_nanos(0_500_000_000),
            utc_max: zx::Time::from_nanos(2_500_000_000),
        };
        assert_eq!(
            ideal_next_poll_time(&bound_3, &RTT_TIMES_ZERO_LATENCY),
            future_monotonic + zx::Duration::from_millis(500),
        );
        assert_eq!(
            ideal_next_poll_time(&bound_3, &RTT_TIMES_100_MS_LATENCY),
            future_monotonic + zx::Duration::from_millis(500) - DURATION_50_MS,
        );
    }

    #[test]
    fn test_ideal_poll_time_in_past() {
        let monotonic_now = zx::Time::get_monotonic();
        let past_monotonic = zx::Time::from_nanos(0);
        let bound = Bound {
            monotonic: past_monotonic,
            utc_min: zx::Time::from_nanos(3_000_000_000),
            utc_max: zx::Time::from_nanos(4_000_000_000),
        };
        // The returned time should be in the future, but the subsecond component should match
        // the otherwise ideal time in the past.
        assert!(ideal_next_poll_time(&bound, &RTT_TIMES_ZERO_LATENCY) > monotonic_now);
        assert_eq!(
            time_subs(ideal_next_poll_time(&bound, &RTT_TIMES_ZERO_LATENCY)),
            time_subs(past_monotonic + zx::Duration::from_millis(500)),
        );
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
            assert_eq!(expected, fake_sampler.produce_sample(1).await);
        }

        // After exhausting canned responses, the sampler should stall.
        assert!(fake_sampler.produce_sample(1).now_or_never().is_none());
        // Completion is signalled via the future provided at construction.
        complete_fut.await;
    }
}
