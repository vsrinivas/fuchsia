// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bound::Bound;
use crate::datatypes::{HttpsSample, Poll};
use crate::Config;
use anyhow::format_err;
use async_trait::async_trait;
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_zircon as zx;
use futures::{future::BoxFuture, lock::Mutex, FutureExt};
use httpdate_hyper::{HttpsDateError, HttpsDateErrorType, NetworkTimeClient};
use hyper::Uri;
use tracing::warn;

const NANOS_IN_SECONDS: i64 = 1_000_000_000;

#[async_trait]
/// An `HttpsDateClient` can make requests against a given uri to retrieve a UTC time.
pub trait HttpsDateClient {
    /// Poll |uri| once to obtain the current UTC time. The time is quantized to a second due to
    /// the format of the HTTP date header.
    async fn request_utc(
        &mut self,
        uri: &Uri,
        https_timeout: zx::Duration,
    ) -> Result<zx::Time, HttpsDateError>;
}

#[async_trait]
impl HttpsDateClient for NetworkTimeClient {
    async fn request_utc(
        &mut self,
        uri: &Uri,
        https_timeout: zx::Duration,
    ) -> Result<zx::Time, HttpsDateError> {
        let utc = self
            .get_network_time(uri.clone())
            .on_timeout(fasync::Time::after(https_timeout), || {
                Err(HttpsDateError::new(HttpsDateErrorType::NetworkError)
                    .with_source(format_err!("Timed out after {:?}", https_timeout)))
            })
            .await?;
        Ok(zx::Time::from_nanos(utc.timestamp_nanos()))
    }
}

/// An `HttpsSampler` produces `HttpsSample`s by polling an HTTP server, possibly by combining
/// the results of multiple polls.
#[async_trait]
pub trait HttpsSampler {
    /// Produce a single `HttpsSample` by polling |num_polls| times. Returns an Ok result
    /// containing a Future as soon as the first poll succeeds. The Future yields a sample. This
    /// signature enables the caller to act on a success as soon as possible without waiting for
    /// all polls to complete.
    async fn produce_sample(
        &self,
        num_polls: usize,
        measure_offset: bool,
    ) -> Result<BoxFuture<'_, HttpsSample>, HttpsDateError>;
}

/// The default implementation of `HttpsSampler` that uses an `HttpsDateClient` to poll a server.
pub struct HttpsSamplerImpl<C: HttpsDateClient> {
    /// Client used to poll servers for time.
    client: Mutex<C>,
    /// URI called to obtain time.
    uri: Uri,
    /// Handle to the system UTC clock. To avoid circular dependencies, this clock should not
    /// be used to generate any data reported to timekeeper, and should only be used for metrics
    /// reported through other means.
    system_clock_for_metrics_only: zx::Clock,
    /// HttpsDate config.
    config: Config,
}

impl HttpsSamplerImpl<NetworkTimeClient> {
    /// Create a new `HttpsSamplerImpl` that makes requests against `uri` to poll time.
    pub fn new(uri: Uri, config: Config) -> Self {
        Self::new_with_client(uri, NetworkTimeClient::new(), config)
    }
}

impl<C: HttpsDateClient + Send> HttpsSamplerImpl<C> {
    fn new_with_client(uri: Uri, client: C, config: Config) -> Self {
        Self {
            client: Mutex::new(client),
            uri,
            system_clock_for_metrics_only: fuchsia_runtime::duplicate_utc_clock_handle(
                zx::Rights::READ,
            )
            .expect("UTC clock handle is invalid"),
            config,
        }
    }
}

#[async_trait]
impl<C: HttpsDateClient + Send> HttpsSampler for HttpsSamplerImpl<C> {
    async fn produce_sample(
        &self,
        num_polls: usize,
        measure_offset: bool,
    ) -> Result<BoxFuture<'_, HttpsSample>, HttpsDateError> {
        // Don't measure offset on the initial poll, as setting up TLS connections causes this poll
        // to take longer.
        let (mut bound, first_poll) = self.poll_server(false).await?;
        let mut polls = vec![first_poll];

        let sample_fut = async move {
            for poll_idx in 1..num_polls {
                let ideal_next_poll_time = ideal_next_poll_time(
                    &bound,
                    polls.iter().map(|poll| &poll.round_trip_time),
                    self.config.first_rtt_time_factor,
                );
                fasync::Timer::new(ideal_next_poll_time).await;

                // For subsequent polls ignore errors. This allows producing a degraded sample
                // instead of outright failing as long as one poll succeeds. In addition,
                // we measure offset only on the second poll. As our algorithm attempts to schedule
                // polls at certain times, measuring offsets for all polls biases the offset
                // towards +/- .5 seconds.
                if let Ok((new_bound, new_poll)) =
                    self.poll_server(measure_offset && poll_idx == 1).await
                {
                    bound = match bound.combine(&new_bound) {
                        Some(combined) => combined,
                        None => {
                            // Bounds might fail to combine if e.g. the device went to sleep and
                            // monotonic time was not updated. We assume the most recent poll is
                            // most accurate and discard accumulated information.
                            // TODO(satsukiu): report this event to Cobalt
                            polls.clear();
                            warn!("Unable to combine time bound, time may have moved.");
                            new_bound
                        }
                    };
                    polls.push(new_poll);
                }
            }

            HttpsSample {
                utc: bound.center(),
                monotonic: bound.monotonic,
                standard_deviation: bound.size() * self.config.standard_deviation_bound_percentage
                    / 100,
                final_bound_size: bound.size(),
                polls,
            }
        };

        Ok(sample_fut.boxed())
    }
}

impl<C: HttpsDateClient + Send> HttpsSamplerImpl<C> {
    /// Poll the server once to produce a fresh bound on the UTC time. Returns a bound and the
    /// observed round trip time.
    async fn poll_server(&self, measure_offset: bool) -> Result<(Bound, Poll), HttpsDateError> {
        let monotonic_before = zx::Time::get_monotonic();
        let reported_utc =
            self.client.lock().await.request_utc(&self.uri, self.config.https_timeout).await?;
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
        let poll = Poll {
            round_trip_time,
            center_offset: if measure_offset {
                Some(
                    bound.center()
                        - time_util::time_at_monotonic(
                            &self.system_clock_for_metrics_only,
                            bound.monotonic,
                        ),
                )
            } else {
                None
            },
        };
        Ok((bound, poll))
    }
}

/// Given a bound and observed round trip times, estimates the ideal monotonic time at which
/// to poll the server.
fn ideal_next_poll_time<'a, I>(
    bound: &Bound,
    mut observed_rtt: I,
    first_rtt_time_factor: u16,
) -> zx::Time
where
    I: Iterator<Item = &'a zx::Duration> + ExactSizeIterator,
{
    // Estimate the ideal monotonic time we'd like the server to check time.
    // ideal_server_check_time is a monotonic time at which bound's projection is centered
    // around a whole number of UTC seconds (utc_min = n - k, utc_max = n + k) where n is a
    // whole number of seconds.
    // Ignoring network latency, the bound produced by polling the server at
    // ideal_server_check_time must be [n-1, n) or [n, n + 1). In either case combining with
    // the original bound results in a bound half the size of the original.
    let ideal_server_check_time =
        bound.monotonic + zx::Duration::from_seconds(1) - time_subs(bound.center());

    // Since there is actually network latency, try to guess what it'll be and start polling
    // early so the server checks at the ideal time. The first poll takes longer than subsequent
    // polls due to TLS handshaking, so we make a best effort to account for that when the first
    // poll is the only one available. Otherwise, we discard the first poll rtt.
    let rtt_guess = match observed_rtt.len() {
        0 => return zx::Time::get_monotonic(),
        1 => *observed_rtt.next().unwrap() / first_rtt_time_factor,
        _ => avg(observed_rtt.skip(1)),
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
fn avg<'a, I>(durations: I) -> zx::Duration
where
    I: ExactSizeIterator + Iterator<Item = &'a zx::Duration>,
{
    let count = durations.len() as i64;
    zx::Duration::from_nanos(durations.map(|d| d.into_nanos()).sum::<i64>() / count)
}

/// Returns the whole second component of a zx::Duration.
fn seconds(duration: zx::Duration) -> zx::Duration {
    duration - subs(duration)
}

/// Returns the subsecond component of a zx::Duration.
fn subs(duration: zx::Duration) -> zx::Duration {
    zx::Duration::from_nanos(duration.into_nanos() % NANOS_IN_SECONDS)
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
        /// List of `produce_sample` request arguments received.
        received_request_num_polls: Mutex<Vec<(usize, bool)>>,
    }

    #[async_trait]
    impl HttpsSampler for FakeSampler {
        async fn produce_sample(
            &self,
            num_polls: usize,
            measure_offset: bool,
        ) -> Result<BoxFuture<'_, HttpsSample>, HttpsDateError> {
            match self.enqueued_responses.lock().await.pop_front() {
                Some(result) => {
                    self.received_request_num_polls.lock().await.push((num_polls, measure_offset));
                    result.map(|sample| futures::future::ready(sample).boxed())
                }
                None => {
                    self.completion_notifier.lock().await.take().unwrap().send(()).unwrap();
                    pending().await
                }
            }
        }
    }

    #[async_trait]
    impl<T: AsRef<FakeSampler> + Send + Sync> HttpsSampler for T {
        async fn produce_sample(
            &self,
            num_polls: usize,
            measure_offset: bool,
        ) -> Result<BoxFuture<'_, HttpsSample>, HttpsDateError> {
            self.as_ref().produce_sample(num_polls, measure_offset).await
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
                received_request_num_polls: Mutex::new(vec![]),
            };
            (client, receiver)
        }

        /// Assert that calls to produce_sample were made with the expected num_polls arguments.
        pub async fn assert_produce_sample_requests(&self, expected: &[(usize, bool)]) {
            assert_eq!(self.received_request_num_polls.lock().await.as_slice(), expected);
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::Config;
    use futures::{FutureExt, TryFutureExt};
    use lazy_static::lazy_static;
    use std::{collections::VecDeque, iter::FromIterator};

    lazy_static! {
        static ref TEST_URI: hyper::Uri = "https://localhost/".parse().unwrap();
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

    fn make_test_config() -> Config {
        Config {
            https_timeout: zx::Duration::from_seconds(10),
            standard_deviation_bound_percentage: 30,
            first_rtt_time_factor: 5,
            use_pull_api: false,
        }
    }

    #[async_trait]
    impl HttpsDateClient for TestClient {
        async fn request_utc(
            &mut self,
            _uri: &Uri,
            _https_timeout: zx::Duration,
        ) -> Result<zx::Time, HttpsDateError> {
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

    #[fuchsia::test(allow_stalls = false)]
    async fn test_produce_sample_one_poll() {
        let config = make_test_config();
        let standard_deviation_bound_percentage = config.standard_deviation_bound_percentage;
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![Ok(TEST_UTC_OFFSET)]),
            config,
        );
        let monotonic_before = zx::Time::get_monotonic();
        let sample = sampler.produce_sample(1, false).await.unwrap().await;
        let monotonic_after = zx::Time::get_monotonic();

        assert!(sample.utc >= monotonic_before + TEST_UTC_OFFSET - ONE_SECOND);
        assert!(sample.utc <= monotonic_after + TEST_UTC_OFFSET + ONE_SECOND);

        assert!(sample.monotonic >= monotonic_before);
        assert!(sample.monotonic <= monotonic_after);
        assert_eq!(
            sample.standard_deviation,
            sample.final_bound_size * standard_deviation_bound_percentage / 100
        );
        assert!(sample.final_bound_size <= monotonic_after - monotonic_before + ONE_SECOND);
        assert_eq!(sample.polls.len(), 1);
        assert!(sample.polls[0].round_trip_time <= monotonic_after - monotonic_before);
        assert!(sample.polls[0].center_offset.is_none());
    }

    #[fuchsia::test]
    async fn test_produce_sample_multiple_polls() {
        let config = make_test_config();
        let standard_deviation_bound_percentage = config.standard_deviation_bound_percentage;
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
            ]),
            config,
        );
        let monotonic_before = zx::Time::get_monotonic();
        let sample = sampler.produce_sample(3, false).await.unwrap().await;
        let monotonic_after = zx::Time::get_monotonic();

        assert!(sample.utc >= monotonic_before + TEST_UTC_OFFSET - ONE_SECOND);
        assert!(sample.utc <= monotonic_after + TEST_UTC_OFFSET + ONE_SECOND);

        assert!(sample.monotonic >= monotonic_before);
        assert!(sample.monotonic <= monotonic_after);
        assert_eq!(
            sample.standard_deviation,
            sample.final_bound_size * standard_deviation_bound_percentage / 100
        );
        assert!(sample.final_bound_size <= monotonic_after - monotonic_before + ONE_SECOND);
        assert_eq!(sample.polls.len(), 3);
        assert!(sample
            .polls
            .iter()
            .all(|poll| poll.round_trip_time <= monotonic_after - monotonic_before));
        assert!(sample.polls.iter().all(|poll| poll.center_offset.is_none()));
    }

    #[fuchsia::test]
    async fn test_produce_sample_offsets() {
        // Create our own test clock that exactly matches the offsets reported by the test
        // client. (Both report UTC time as (monotonic + offset))
        // Since the 'source' reported by the test client and the test clock are
        // synchronized we can assert that the reported samples are within some bound.
        let test_clock = zx::Clock::create(zx::ClockOpts::empty(), None).unwrap();
        let monotonic_ref = zx::Time::get_monotonic();
        test_clock
            .update(
                zx::ClockUpdate::builder()
                    .absolute_value(monotonic_ref, monotonic_ref + TEST_UTC_OFFSET),
            )
            .unwrap();
        let config = make_test_config();

        let sampler = HttpsSamplerImpl {
            uri: TEST_URI.clone(),
            client: Mutex::new(TestClient::with_offset_responses(vec![
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
            ])),
            system_clock_for_metrics_only: test_clock,
            config,
        };

        let sample = sampler.produce_sample(3, true).await.unwrap().await;

        // only the second poll has an offset.
        let first_poll = sample.polls[0].clone();
        assert!(first_poll.center_offset.is_none());

        let second_poll = sample.polls[1].clone();
        let offset = second_poll.center_offset.unwrap();
        assert!(offset >= zx::Duration::from_millis(-500) - second_poll.round_trip_time / 2);
        assert!(offset <= zx::Duration::from_millis(500) + second_poll.round_trip_time / 2);

        assert!(sample.polls.iter().skip(2).all(|poll| poll.center_offset.is_none()));
    }

    #[fuchsia::test]
    async fn test_produce_sample_fails_if_initial_poll_fails() {
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![Err(HttpsDateError::new(
                HttpsDateErrorType::NetworkError,
            ))]),
            make_test_config(),
        );

        match sampler.produce_sample(3, false).await {
            Ok(_) => panic!("Expected error but received Ok"),
            Err(e) => assert_eq!(e.error_type(), HttpsDateErrorType::NetworkError),
        };
    }

    #[fuchsia::test]
    async fn test_produce_sample_succeeds_if_subsequent_poll_fails() {
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
                Err(HttpsDateError::new(HttpsDateErrorType::NetworkError)),
            ]),
            make_test_config(),
        );

        let sample = sampler.produce_sample(3, false).await.unwrap().await;
        assert_eq!(sample.polls.len(), 2);
    }

    #[fuchsia::test]
    async fn test_produce_sample_takes_later_poll_if_polls_disagree() {
        let expected_offset = TEST_UTC_OFFSET + zx::Duration::from_hours(1);
        let sampler = HttpsSamplerImpl::new_with_client(
            TEST_URI.clone(),
            TestClient::with_offset_responses(vec![
                Ok(TEST_UTC_OFFSET),
                Ok(TEST_UTC_OFFSET),
                Ok(expected_offset),
            ]),
            make_test_config(),
        );

        let monotonic_before = zx::Time::get_monotonic();
        let sample = sampler.produce_sample(3, false).await.unwrap().await;
        let monotonic_after = zx::Time::get_monotonic();

        assert_eq!(sample.polls.len(), 1);
        assert!(sample.utc >= monotonic_before + expected_offset - ONE_SECOND);
        assert!(sample.utc <= monotonic_after + expected_offset + ONE_SECOND);
    }

    #[fuchsia::test]
    fn test_ideal_poll_time_in_future() {
        let future_monotonic = zx::Time::get_monotonic() + zx::Duration::from_hours(100);
        let first_rtt_time_factor = make_test_config().first_rtt_time_factor;
        let bound_1 = Bound {
            monotonic: future_monotonic,
            utc_min: zx::Time::from_nanos(3_000_000_000),
            utc_max: zx::Time::from_nanos(4_000_000_000),
        };
        assert_eq!(
            ideal_next_poll_time(&bound_1, RTT_TIMES_ZERO_LATENCY.iter(), first_rtt_time_factor),
            future_monotonic + zx::Duration::from_millis(500),
        );
        assert_eq!(
            ideal_next_poll_time(&bound_1, RTT_TIMES_100_MS_LATENCY.iter(), first_rtt_time_factor),
            future_monotonic + zx::Duration::from_millis(500) - DURATION_50_MS,
        );

        let bound_2 = Bound {
            monotonic: future_monotonic,
            utc_min: zx::Time::from_nanos(3_600_000_000),
            utc_max: zx::Time::from_nanos(3_800_000_000),
        };
        assert_eq!(
            ideal_next_poll_time(&bound_2, RTT_TIMES_ZERO_LATENCY.iter(), first_rtt_time_factor),
            future_monotonic + zx::Duration::from_millis(300),
        );
        assert_eq!(
            ideal_next_poll_time(&bound_2, RTT_TIMES_100_MS_LATENCY.iter(), first_rtt_time_factor),
            future_monotonic + zx::Duration::from_millis(300) - DURATION_50_MS,
        );

        let bound_3 = Bound {
            monotonic: future_monotonic,
            utc_min: zx::Time::from_nanos(0_500_000_000),
            utc_max: zx::Time::from_nanos(2_500_000_000),
        };
        assert_eq!(
            ideal_next_poll_time(&bound_3, RTT_TIMES_ZERO_LATENCY.iter(), first_rtt_time_factor),
            future_monotonic + zx::Duration::from_millis(500),
        );
        assert_eq!(
            ideal_next_poll_time(&bound_3, RTT_TIMES_100_MS_LATENCY.iter(), first_rtt_time_factor),
            future_monotonic + zx::Duration::from_millis(500) - DURATION_50_MS,
        );
    }

    #[fuchsia::test]
    fn test_ideal_poll_time_in_past() {
        let monotonic_now = zx::Time::get_monotonic();
        let past_monotonic = zx::Time::from_nanos(0);
        let first_rtt_time_factor = make_test_config().first_rtt_time_factor;
        let bound = Bound {
            monotonic: past_monotonic,
            utc_min: zx::Time::from_nanos(3_000_000_000),
            utc_max: zx::Time::from_nanos(4_000_000_000),
        };
        // The returned time should be in the future, but the subsecond component should match
        // the otherwise ideal time in the past.
        assert!(
            ideal_next_poll_time(&bound, RTT_TIMES_ZERO_LATENCY.iter(), first_rtt_time_factor)
                > monotonic_now
        );
        assert_eq!(
            time_subs(ideal_next_poll_time(
                &bound,
                RTT_TIMES_ZERO_LATENCY.iter(),
                first_rtt_time_factor
            )),
            time_subs(past_monotonic + zx::Duration::from_millis(500)),
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_fake_sampler() {
        let expected_responses = vec![
            Ok(HttpsSample {
                utc: zx::Time::from_nanos(999),
                monotonic: zx::Time::from_nanos(888_888_888),
                standard_deviation: zx::Duration::from_nanos(22),
                final_bound_size: zx::Duration::from_nanos(44),
                polls: vec![Poll {
                    round_trip_time: zx::Duration::from_nanos(55),
                    center_offset: Some(zx::Duration::from_nanos(100)),
                }],
            }),
            Err(HttpsDateErrorType::NetworkError),
            Err(HttpsDateErrorType::NoCertificatesPresented),
        ];
        let (fake_sampler, complete_fut) = FakeSampler::with_responses(
            expected_responses
                .iter()
                .cloned()
                .map(|response| response.map_err(HttpsDateError::new))
                .collect::<Vec<_>>(),
        );
        for expected in expected_responses {
            assert_eq!(
                expected,
                fake_sampler
                    .produce_sample(1, false)
                    .and_then(|sample_fut| async move { Ok(sample_fut.await) })
                    .await
                    .map_err(|e| e.error_type())
            );
        }

        // After exhausting canned responses, the sampler should stall.
        assert!(fake_sampler.produce_sample(1, false).now_or_never().is_none());
        // Completion is signalled via the future provided at construction.
        complete_fut.await;
    }
}
