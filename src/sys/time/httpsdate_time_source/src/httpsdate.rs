// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::{
            CONVERGE_SAMPLES, INITIAL_SAMPLE_POLLS, MAX_TIME_BETWEEN_SAMPLES_RANDOMIZATION,
            SAMPLE_POLLS,
        },
        datatypes::{HttpsSample, Phase},
        diagnostics::{Diagnostics, Event},
        sampler::HttpsSampler,
        Config,
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_time_external::{Properties, Status, TimeSample, Urgency},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{channel::mpsc::Sender, future::BoxFuture, lock::Mutex, Future, SinkExt},
    httpdate_hyper::{HttpsDateError, HttpsDateErrorType},
    push_source::Update,
    rand::Rng,
    tracing::{error, info, warn},
};

/// A definition of how long an algorithm should wait between polls. Defines fixed wait durations
/// following successful poll attempts, and a capped exponential backoff following failed poll
/// attempts.
pub struct RetryStrategy {
    pub min_between_failures: zx::Duration,
    pub max_exponent: u32,
    pub tries_per_exponent: u32,
    pub converge_time_between_samples: zx::Duration,
    pub maintain_time_between_samples: zx::Duration,
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
pub struct HttpsDateUpdateAlgorithm<
    'a,
    S: HttpsSampler + Send + Sync,
    D: Diagnostics,
    N: Future<Output = Result<(), Error>> + Send,
> {
    /// Strategy defining how long to wait after successes and failures.
    retry_strategy: RetryStrategy,
    /// Sampler used to produce samples.
    sampler: S,
    /// Object managing diagnostics output.
    diagnostics: D,
    /// Future that completes when the network is available. A 'None' value indicates the network
    /// check has previously completed.
    network_available_fut: Mutex<Option<N>>,
    /// HttpsDate config.
    config: &'a Config,
}

impl<'a, S, D, N> HttpsDateUpdateAlgorithm<'a, S, D, N>
where
    S: HttpsSampler + Send + Sync,
    D: Diagnostics,
    N: Future<Output = Result<(), Error>> + Send,
{
    async fn handle_produce_sample(&self, sample_fut: BoxFuture<'_, HttpsSample>) -> HttpsSample {
        let sample = sample_fut.await;
        info!(
            "Got a time sample - UTC {:?}, bound size {:?}, and polls {:?}",
            sample.utc, sample.final_bound_size, sample.polls
        );
        self.diagnostics.record(Event::Success(&sample));
        sample
    }

    // Returns `Some(new_status)` if the error changes the current status.
    fn handle_sample_error(
        &self,
        http_error: HttpsDateError,
        last_error_type: &mut Option<HttpsDateErrorType>,
    ) -> Option<Status> {
        let error_type = http_error.error_type();
        self.diagnostics.record(Event::Failure(error_type));
        if Some(error_type) != *last_error_type {
            *last_error_type = Some(error_type);
            let new_status = match error_type {
                HttpsDateErrorType::InvalidHostname | HttpsDateErrorType::SchemeNotHttps => {
                    // TODO(fxbug.dev/59771) - decide how to surface irrecoverable
                    // errors to clients
                    error!(
                        "Got an unexpected error {:?}, which indicates a bad \
                             configuration.",
                        http_error
                    );
                    Status::UnknownUnhealthy
                }
                HttpsDateErrorType::NetworkError => {
                    warn!("Failed to poll time: {:?}", http_error);
                    Status::Network
                }
                _ => {
                    warn!("Failed to poll time: {:?}", http_error);
                    Status::Protocol
                }
            };
            Some(new_status)
        } else {
            None
        }
    }
}

#[async_trait]
impl<'a, S, D, N> push_source::UpdateAlgorithm for HttpsDateUpdateAlgorithm<'a, S, D, N>
where
    S: HttpsSampler + Send + Sync,
    D: Diagnostics,
    N: Future<Output = Result<(), Error>> + Send,
{
    async fn update_device_properties(&self, _properties: Properties) {
        // since our samples are polled independently for now, we don't need to use
        // device properties yet.
    }

    async fn generate_updates(&self, mut sink: Sender<Update>) -> Result<(), Error> {
        let mut network_available_lock = self.network_available_fut.lock().await;
        if let Some(fut) = network_available_lock.take() {
            info!("Waiting for network to become available.");
            match fut.await {
                Ok(()) => {
                    info!("Network check completed.");
                    sink.send(Status::Ok.into()).await?;
                    self.diagnostics.record(Event::NetworkCheckSuccessful);
                }
                Err(e) => {
                    error!("Network check failed, polling for time anyway: {:?}", e);
                    sink.send(Status::Network.into()).await?;
                }
            }
        }
        drop(network_available_lock);

        // randomize poll timings somewhat so polls across devices will not be synchronized
        let random_factor = 1f32
            + rand::thread_rng().gen_range(
                -MAX_TIME_BETWEEN_SAMPLES_RANDOMIZATION..MAX_TIME_BETWEEN_SAMPLES_RANDOMIZATION,
            );
        let converge_time_between_samples =
            mult_duration(self.retry_strategy.converge_time_between_samples, random_factor);
        let maintain_time_between_samples =
            mult_duration(self.retry_strategy.maintain_time_between_samples, random_factor);

        self.diagnostics.record(Event::Phase(Phase::Initial));
        self.try_generate_sample_until_successful(INITIAL_SAMPLE_POLLS, &mut sink).await?;

        self.diagnostics.record(Event::Phase(Phase::Converge));
        for _ in 0..CONVERGE_SAMPLES {
            fasync::Timer::new(fasync::Time::after(converge_time_between_samples)).await;
            self.try_generate_sample_until_successful(SAMPLE_POLLS, &mut sink).await?;
        }

        self.diagnostics.record(Event::Phase(Phase::Maintain));
        loop {
            fasync::Timer::new(fasync::Time::after(maintain_time_between_samples)).await;
            self.try_generate_sample_until_successful(SAMPLE_POLLS, &mut sink).await?;
        }
    }
}

#[async_trait]
impl<'a, S, D, N> pull_source::UpdateAlgorithm for HttpsDateUpdateAlgorithm<'a, S, D, N>
where
    S: HttpsSampler + Send + Sync,
    D: Diagnostics,
    N: Future<Output = Result<(), Error>> + Send,
{
    async fn update_device_properties(&self, _properties: Properties) {
        // since our samples are polled independently for now, we don't need to use
        // device properties yet.
    }

    async fn sample(&self, urgency: Urgency) -> Result<TimeSample, pull_source::SampleError> {
        let sample_config =
            self.config.sample_config_by_urgency.get(&urgency).ok_or_else(|| {
                error!("No config for urgency level {:?}", urgency);
                pull_source::SampleError::Internal
            })?;
        let num_attempts = sample_config.max_attempts;
        let num_polls: usize = sample_config.num_polls.try_into().map_err(|e| {
            error!("num_polls numeric overflow: {:?}", e);
            pull_source::SampleError::Internal
        })?;
        let mut last_error_type = None;
        let mut attempt_iter = 0u32..num_attempts;
        loop {
            let attempt = match attempt_iter.next() {
                Some(a) => a,
                None => match last_error_type {
                    None => {
                        warn!("Exhausted attempts to fetch a sample, empty last error type.");
                        return Err(pull_source::SampleError::Internal);
                    }
                    Some(e) => {
                        warn!("Exhausted attempts to fetch a sample, last error type {:?}.", e);
                        match e {
                            HttpsDateErrorType::InvalidHostname
                            | HttpsDateErrorType::SchemeNotHttps => {
                                return Err(pull_source::SampleError::Internal)
                            }
                            _ => return Err(pull_source::SampleError::Network),
                        }
                    }
                },
            };
            match self.sampler.produce_sample(num_polls).await {
                Ok(sample_fut) => {
                    return Ok(self.handle_produce_sample(sample_fut).await.into());
                }
                Err(http_error) => {
                    let _ = self.handle_sample_error(http_error, &mut last_error_type);
                }
            }
            fasync::Timer::new(fasync::Time::after(self.retry_strategy.backoff_duration(attempt)))
                .await;
        }
    }

    async fn next_possible_sample_time(&self) -> zx::Time {
        // TODO(fxb/113722): Implement rate limiting if required.
        zx::Time::get_monotonic()
    }
}

impl<'a, S, D, N> HttpsDateUpdateAlgorithm<'a, S, D, N>
where
    S: HttpsSampler + Send + Sync,
    D: Diagnostics,
    N: Future<Output = Result<(), Error>> + Send,
{
    pub fn new(
        retry_strategy: RetryStrategy,
        diagnostics: D,
        sampler: S,
        network_available_fut: N,
        config: &'a Config,
    ) -> Self {
        Self {
            retry_strategy,
            sampler,
            diagnostics,
            network_available_fut: Mutex::new(Some(network_available_fut)),
            config,
        }
    }

    /// Repeatedly poll for a time until one sample is successfully retrieved. Pushes updates to
    /// |sink|.
    async fn try_generate_sample_until_successful(
        &self,
        num_polls: usize,
        sink: &mut Sender<Update>,
    ) -> Result<(), Error> {
        let mut attempt_iter = 0u32..;
        let mut last_error_type = None;
        loop {
            let attempt = attempt_iter.next().unwrap_or(u32::MAX);
            match self.sampler.produce_sample(num_polls).await {
                Ok(sample_fut) => {
                    sink.send(Status::Ok.into()).await?;
                    let sample = self.handle_produce_sample(sample_fut).await;
                    sink.send(sample.into()).await?;
                    return Ok(());
                }
                Err(http_error) => {
                    if let Some(new_status) =
                        self.handle_sample_error(http_error, &mut last_error_type)
                    {
                        sink.send(new_status.into()).await?;
                    }
                }
            }
            fasync::Timer::new(fasync::Time::after(self.retry_strategy.backoff_duration(attempt)))
                .await;
        }
    }
}

fn mult_duration(duration: zx::Duration, factor: f32) -> zx::Duration {
    let nanos_float = (duration.into_nanos() as f64) * factor as f64;
    zx::Duration::from_nanos(nanos_float as i64)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            datatypes::{HttpsSample, Poll},
            diagnostics::FakeDiagnostics,
            sampler::FakeSampler,
            SampleConfig,
        },
        anyhow::format_err,
        assert_matches::assert_matches,
        fidl_fuchsia_time_external::TimeSample,
        futures::{channel::mpsc::channel, future::ready, stream::StreamExt, task::Poll as FPoll},
        httpdate_hyper::HttpsDateError,
        lazy_static::lazy_static,
        pull_source::UpdateAlgorithm as _,
        push_source::UpdateAlgorithm as _,
        std::sync::Arc,
    };

    /// Test retry strategy with minimal wait periods.
    const TEST_RETRY_STRATEGY: RetryStrategy = RetryStrategy {
        min_between_failures: zx::Duration::from_nanos(100),
        max_exponent: 1,
        tries_per_exponent: 1,
        converge_time_between_samples: zx::Duration::from_nanos(100),
        maintain_time_between_samples: zx::Duration::from_nanos(100),
    };

    lazy_static! {
        static ref TEST_SAMPLE_1: HttpsSample = HttpsSample {
            utc: zx::Time::from_nanos(111_222_333_444_555),
            monotonic: zx::Time::from_nanos(666_777_888_999_000),
            standard_deviation: zx::Duration::from_millis(101),
            final_bound_size: zx::Duration::from_millis(20),
            polls: vec![],
        };
        static ref TEST_SAMPLE_2: HttpsSample = HttpsSample {
            utc: zx::Time::from_nanos(999_999_999_999_999),
            monotonic: zx::Time::from_nanos(777_777_777_777_777),
            standard_deviation: zx::Duration::from_millis(102),
            final_bound_size: zx::Duration::from_millis(30),
            polls: vec![Poll { round_trip_time: zx::Duration::from_millis(23) }],
        };
    }

    fn to_fidl_time_sample(sample: &HttpsSample) -> TimeSample {
        TimeSample {
            utc: Some(sample.utc.into_nanos()),
            monotonic: Some(sample.monotonic.into_nanos()),
            standard_deviation: Some(sample.standard_deviation.into_nanos()),
            ..TimeSample::EMPTY
        }
    }

    fn to_fidl_time_sample_arc(sample: &HttpsSample) -> Arc<TimeSample> {
        Arc::new(to_fidl_time_sample(sample))
    }

    fn make_test_config() -> Config {
        let sample_config_by_urgency = [
            (Urgency::Low, SampleConfig { max_attempts: 2, num_polls: 2 }),
            (Urgency::Medium, SampleConfig { max_attempts: 3, num_polls: 3 }),
            (Urgency::High, SampleConfig { max_attempts: 5, num_polls: 5 }),
        ]
        .into_iter()
        .collect();
        Config {
            https_timeout: zx::Duration::from_seconds(10),
            standard_deviation_bound_percentage: 30,
            first_rtt_time_factor: 5,
            use_pull_api: false,
            sample_config_by_urgency,
        }
    }

    #[fuchsia::test]
    fn test_retry_strategy() {
        let strategy = RetryStrategy {
            min_between_failures: zx::Duration::from_seconds(1),
            max_exponent: 3,
            tries_per_exponent: 3,
            converge_time_between_samples: zx::Duration::from_seconds(10),
            maintain_time_between_samples: zx::Duration::from_seconds(10),
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

    #[fuchsia::test]
    fn test_update_task_blocks_until_update_processed() {
        // Tests that our update loop blocks execution when run using a channel with zero capacity
        // as is done from PushSource. This verifies that each update is processed before another
        // is produced.
        // TODO(satsukiu) - use a generator instead and remove this test.
        let mut executor = fasync::TestExecutor::new().unwrap();

        let (sampler, _response_complete_fut) =
            FakeSampler::with_responses(vec![Ok(TEST_SAMPLE_1.clone()), Ok(TEST_SAMPLE_2.clone())]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let config = &make_test_config();
        let update_algorithm = HttpsDateUpdateAlgorithm::new(
            TEST_RETRY_STRATEGY,
            diagnostics,
            sampler,
            ready(Ok(())),
            config,
        );
        let (sender, mut receiver) = channel(0);
        let mut update_fut = update_algorithm.generate_updates(sender);

        // After running to a stall, the network check complete update is available
        assert!(executor.run_until_stalled(&mut update_fut).is_pending());
        assert_eq!(
            executor.run_until_stalled(&mut receiver.next()),
            FPoll::Ready(Some(Status::Ok.into()))
        );
        assert!(executor.run_until_stalled(&mut receiver.next()).is_pending());

        // After running to a stall again, the first update is available
        assert!(executor.run_until_stalled(&mut update_fut).is_pending());
        assert_eq!(
            executor.run_until_stalled(&mut receiver.next()),
            FPoll::Ready(Some(Status::Ok.into()))
        );
        assert!(executor.run_until_stalled(&mut receiver.next()).is_pending());

        // Running the update task again to a stall produces a second update.
        assert!(executor.run_until_stalled(&mut update_fut).is_pending());
        assert_matches!(
            executor.run_until_stalled(&mut receiver.next()),
            FPoll::Ready(Some(Update::Sample(_)))
        );
    }

    #[fuchsia::test]
    async fn test_successful_updates() {
        let expected_samples = vec![TEST_SAMPLE_1.clone(), TEST_SAMPLE_2.clone()];
        let (sampler, response_complete_fut) =
            FakeSampler::with_responses(expected_samples.iter().map(|sample| Ok(sample.clone())));
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let (sender, receiver) = channel(0);
        let _update_task = fasync::Task::spawn({
            let diagnostics = Arc::clone(&diagnostics);
            async move {
                let config = make_test_config();
                let update_algorithm = HttpsDateUpdateAlgorithm::new(
                    TEST_RETRY_STRATEGY,
                    diagnostics,
                    sampler,
                    ready(Ok(())),
                    &config,
                );
                update_algorithm.generate_updates(sender).await
            }
        });
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
        assert_eq!(
            samples,
            expected_samples.iter().map(to_fidl_time_sample_arc).collect::<Vec<_>>()
        );
        let expected_events = vec![
            Event::NetworkCheckSuccessful,
            Event::Phase(Phase::Initial),
            Event::Success(&*TEST_SAMPLE_1),
            Event::Phase(Phase::Converge),
            Event::Success(&*TEST_SAMPLE_2),
        ];
        diagnostics.assert_events(expected_events);
    }

    #[fuchsia::test]
    async fn test_retry_until_successful() {
        let injected_responses = vec![
            Err(HttpsDateError::new(HttpsDateErrorType::NetworkError)),
            Err(HttpsDateError::new(HttpsDateErrorType::NetworkError)),
            Err(HttpsDateError::new(HttpsDateErrorType::NoCertificatesPresented)),
            Ok(TEST_SAMPLE_1.clone()),
        ];
        let (sampler, response_complete_fut) = FakeSampler::with_responses(injected_responses);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let (sender, receiver) = channel(0);
        let _update_task = fasync::Task::spawn({
            let diagnostics = Arc::clone(&diagnostics);
            async move {
                let config = make_test_config();
                let update_algorithm = HttpsDateUpdateAlgorithm::new(
                    TEST_RETRY_STRATEGY,
                    diagnostics,
                    sampler,
                    ready(Ok(())),
                    &config,
                );
                update_algorithm.generate_updates(sender).await
            }
        });
        let updates = receiver.take_until(response_complete_fut).collect::<Vec<_>>().await;

        // The initial OK from the network check, and each injected status should be reported.
        let expected_status_updates: Vec<Update> = vec![
            Status::Ok.into(),
            Status::Network.into(),
            Status::Protocol.into(),
            Status::Ok.into(),
        ];
        let received_status_updates =
            updates.iter().filter(|updates| updates.is_status()).cloned().collect::<Vec<_>>();
        assert_eq!(expected_status_updates, received_status_updates);

        // Last update should be the new sample.
        let last_update = updates.iter().last().unwrap();
        match last_update {
            Update::Sample(sample) => assert_eq!(*sample, to_fidl_time_sample_arc(&*TEST_SAMPLE_1)),
            Update::Status(_) => panic!("Expected a sample but got an update"),
        }

        let expected_events = vec![
            Event::NetworkCheckSuccessful,
            Event::Phase(Phase::Initial),
            Event::Failure(HttpsDateErrorType::NetworkError),
            Event::Failure(HttpsDateErrorType::NetworkError),
            Event::Failure(HttpsDateErrorType::NoCertificatesPresented),
            Event::Success(&TEST_SAMPLE_1),
        ];
        // depending on how futures get polled, there may or may not be an event to update the
        // phase to converge.
        diagnostics.assert_events_starts_with(expected_events);
    }

    #[fuchsia::test]
    async fn test_poll_even_if_network_check_fails() {
        let injected_responses = vec![Ok(TEST_SAMPLE_1.clone())];
        let (sampler, response_complete_fut) = FakeSampler::with_responses(injected_responses);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let (sender, receiver) = channel(0);
        let _update_task = fasync::Task::spawn({
            let diagnostics = Arc::clone(&diagnostics);
            async move {
                let config = make_test_config();
                let update_algorithm = HttpsDateUpdateAlgorithm::new(
                    TEST_RETRY_STRATEGY,
                    diagnostics,
                    sampler,
                    ready(Err(format_err!("network check error"))),
                    &config,
                );
                update_algorithm.generate_updates(sender).await
            }
        });
        let updates = receiver.take_until(response_complete_fut).collect::<Vec<_>>().await;

        // The initial error from the network check, and OK for the sample should be reported.
        let expected_status_updates: Vec<Update> = vec![Status::Network.into(), Status::Ok.into()];
        let received_status_updates =
            updates.iter().filter(|updates| updates.is_status()).cloned().collect::<Vec<_>>();
        assert_eq!(expected_status_updates, received_status_updates);

        // Last update should be the new sample.
        let last_update = updates.iter().last().unwrap();
        match last_update {
            Update::Sample(sample) => assert_eq!(*sample, to_fidl_time_sample_arc(&*TEST_SAMPLE_1)),
            Update::Status(_) => panic!("Expected a sample but got an update"),
        }

        // Network check event not emitted if the check fails.
        diagnostics.assert_events_starts_with(vec![Event::Phase(Phase::Initial)]);
    }

    #[fuchsia::test]
    async fn test_phases() {
        let expected_num_samples = 1 /*initial*/ + CONVERGE_SAMPLES + 1 /*maintain*/;
        let expected_samples = vec![TEST_SAMPLE_1.clone(); expected_num_samples];

        let (sampler, response_complete_fut) =
            FakeSampler::with_responses(expected_samples.iter().map(|sample| Ok(sample.clone())));
        let sampler = Arc::new(sampler);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let (sender, receiver) = channel(0);
        let _update_task = fasync::Task::spawn({
            let diagnostics = Arc::clone(&diagnostics);
            let sampler = Arc::clone(&sampler);
            async move {
                let config = make_test_config();
                let update_algorithm = HttpsDateUpdateAlgorithm::new(
                    TEST_RETRY_STRATEGY,
                    diagnostics,
                    sampler,
                    ready(Ok(())),
                    &config,
                );
                update_algorithm.generate_updates(sender).await
            }
        });
        let updates = receiver.take_until(response_complete_fut).collect::<Vec<_>>().await;

        // All status updates should indicate OK.
        assert!(updates
            .iter()
            .filter(|update| update.is_status())
            .all(|update| *update == Update::Status(Status::Ok)));

        let expected_events = vec![
            vec![
                Event::NetworkCheckSuccessful,
                Event::Phase(Phase::Initial),
                Event::Success(&TEST_SAMPLE_1),
                Event::Phase(Phase::Converge),
            ],
            vec![Event::Success(&TEST_SAMPLE_1); CONVERGE_SAMPLES],
            vec![Event::Phase(Phase::Maintain), Event::Success(&TEST_SAMPLE_1)],
        ]
        .concat();
        diagnostics.assert_events(expected_events);

        // Samples should be requested using the number of polls appropriate for the phase.
        // Samples should be requested with offset metrics only in the maintain phase.
        let expected_sample_requests = vec![
            vec![INITIAL_SAMPLE_POLLS],
            vec![SAMPLE_POLLS; CONVERGE_SAMPLES],
            vec![SAMPLE_POLLS],
        ]
        .concat();
        sampler.assert_produce_sample_requests(&expected_sample_requests).await;
    }

    #[fuchsia::test]
    async fn test_pull_sample() {
        let expected_num_samples = 1;
        let expected_samples = vec![TEST_SAMPLE_1.clone(); expected_num_samples];

        let (sampler, _response_complete_fut) =
            FakeSampler::with_responses(expected_samples.iter().map(|sample| Ok(sample.clone())));
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let config = make_test_config();
        let update_algorithm = HttpsDateUpdateAlgorithm::new(
            TEST_RETRY_STRATEGY,
            diagnostics,
            sampler,
            ready(Ok(())),
            &config,
        );
        let sample = update_algorithm.sample(Urgency::Medium).await;

        assert_eq!(sample.unwrap(), to_fidl_time_sample(&*TEST_SAMPLE_1));
    }

    #[fuchsia::test]
    async fn test_pull_retries_on_error() {
        let injected_responses = vec![
            Err(HttpsDateError::new(HttpsDateErrorType::NetworkError)),
            Err(HttpsDateError::new(HttpsDateErrorType::NoCertificatesPresented)),
            Ok(TEST_SAMPLE_1.clone()),
        ];
        let (sampler, _response_complete_fut) = FakeSampler::with_responses(injected_responses);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let config = make_test_config();

        let update_algorithm = HttpsDateUpdateAlgorithm::new(
            TEST_RETRY_STRATEGY,
            diagnostics,
            sampler,
            ready(Ok(())),
            &config,
        );
        let sample = update_algorithm.sample(Urgency::Medium).await;

        assert_eq!(sample.unwrap(), to_fidl_time_sample(&*TEST_SAMPLE_1));
    }

    #[fuchsia::test]
    async fn test_pull_exhausts_num_attempts() {
        let injected_responses = vec![
            Err(HttpsDateError::new(HttpsDateErrorType::NetworkError)),
            Err(HttpsDateError::new(HttpsDateErrorType::NetworkError)),
            Err(HttpsDateError::new(HttpsDateErrorType::NoCertificatesPresented)),
            Ok(TEST_SAMPLE_1.clone()),
        ];
        let (sampler, _response_complete_fut) = FakeSampler::with_responses(injected_responses);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let config = make_test_config();

        let update_algorithm = HttpsDateUpdateAlgorithm::new(
            TEST_RETRY_STRATEGY,
            diagnostics,
            sampler,
            ready(Ok(())),
            &config,
        );
        let sample = update_algorithm.sample(Urgency::Medium).await;

        assert_eq!(sample.unwrap_err(), pull_source::SampleError::Network);
    }

    #[fuchsia::test]
    async fn test_pull_produce_sample_requests() {
        let injected_responses =
            vec![Ok(TEST_SAMPLE_1.clone()), Ok(TEST_SAMPLE_1.clone()), Ok(TEST_SAMPLE_1.clone())];
        let (sampler, _response_complete_fut) = FakeSampler::with_responses(injected_responses);
        let sampler = Arc::new(sampler);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let config = make_test_config();

        let update_algorithm = HttpsDateUpdateAlgorithm::new(
            TEST_RETRY_STRATEGY,
            diagnostics,
            Arc::clone(&sampler),
            ready(Ok(())),
            &config,
        );
        let _sample = update_algorithm.sample(Urgency::Low).await;
        let _sample = update_algorithm.sample(Urgency::Medium).await;
        let _sample = update_algorithm.sample(Urgency::High).await;

        let sample_config_low = config.sample_config_by_urgency.get(&Urgency::Low).unwrap();
        let sample_config_medium = config.sample_config_by_urgency.get(&Urgency::Medium).unwrap();
        let sample_config_high = config.sample_config_by_urgency.get(&Urgency::High).unwrap();
        let expected_sample_requests = vec![
            vec![sample_config_low.num_polls as usize],
            vec![sample_config_medium.num_polls as usize],
            vec![sample_config_high.num_polls as usize],
        ]
        .concat();
        sampler.assert_produce_sample_requests(&expected_sample_requests).await;
    }
}
