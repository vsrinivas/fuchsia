// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        datatypes::{HttpsSample, Phase},
        diagnostics::{Diagnostics, Event},
    },
    anyhow::{format_err, Context as _, Error},
    cobalt_client::traits::AsEventCodes,
    fidl_contrib::{
        protocol_connector::ConnectedProtocol, protocol_connector::ProtocolSender,
        ProtocolConnector,
    },
    fidl_fuchsia_metrics::{
        HistogramBucket, MetricEvent, MetricEventLoggerFactoryMarker, MetricEventLoggerProxy,
        ProjectSpec,
    },
    fuchsia_cobalt_builders::MetricEventExt,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    futures::{future, Future, FutureExt as _},
    parking_lot::Mutex,
    time_metrics_registry::{
        HttpsdateBoundSizeMigratedMetricDimensionPhase as CobaltPhase,
        HTTPSDATE_BOUND_SIZE_MIGRATED_METRIC_ID, HTTPSDATE_POLL_LATENCY_MIGRATED_INT_BUCKETS_FLOOR,
        HTTPSDATE_POLL_LATENCY_MIGRATED_INT_BUCKETS_NUM_BUCKETS as RTT_BUCKETS,
        HTTPSDATE_POLL_LATENCY_MIGRATED_INT_BUCKETS_STEP_SIZE,
        HTTPSDATE_POLL_LATENCY_MIGRATED_METRIC_ID, PROJECT_ID,
    },
};

const RTT_BUCKET_SIZE: zx::Duration =
    zx::Duration::from_micros(HTTPSDATE_POLL_LATENCY_MIGRATED_INT_BUCKETS_STEP_SIZE as i64);
const RTT_BUCKET_FLOOR: zx::Duration =
    zx::Duration::from_micros(HTTPSDATE_POLL_LATENCY_MIGRATED_INT_BUCKETS_FLOOR);

struct CobaltConnectedService;
impl ConnectedProtocol for CobaltConnectedService {
    type Protocol = MetricEventLoggerProxy;
    type ConnectError = Error;
    type Message = MetricEvent;
    type SendError = Error;

    fn get_protocol<'a>(
        &'a mut self,
    ) -> future::BoxFuture<'a, Result<MetricEventLoggerProxy, Error>> {
        async {
            let (logger_proxy, server_end) =
                fidl::endpoints::create_proxy().context("failed to create proxy endpoints")?;
            let metric_event_logger_factory =
                connect_to_protocol::<MetricEventLoggerFactoryMarker>()
                    .context("Failed to connect to fuchsia::metrics::MetricEventLoggerFactory")?;

            metric_event_logger_factory
                .create_metric_event_logger(
                    ProjectSpec { project_id: Some(PROJECT_ID), ..ProjectSpec::EMPTY },
                    server_end,
                )
                .await?
                .map_err(|e| format_err!("Connection to MetricEventLogger refused {e:?}"))?;
            Ok(logger_proxy)
        }
        .boxed()
    }

    fn send_message<'a>(
        &'a mut self,
        protocol: &'a MetricEventLoggerProxy,
        mut msg: MetricEvent,
    ) -> future::BoxFuture<'a, Result<(), Error>> {
        async move {
            let fut = protocol.log_metric_events(&mut std::iter::once(&mut msg));
            fut.await?.map_err(|e| format_err!("Failed to log metric {e:?}"))?;
            Ok(())
        }
        .boxed()
    }
}

/// A `Diagnostics` implementation that uploads diagnostics metrics to Cobalt.
pub struct CobaltDiagnostics {
    /// Client connection to Cobalt.
    sender: Mutex<ProtocolSender<MetricEvent>>,
    /// Last known phase of the algorithm.
    phase: Mutex<Phase>,
}

impl CobaltDiagnostics {
    /// Create a new `CobaltDiagnostics`, and future that must be polled to upload to Cobalt.
    pub fn new() -> (Self, impl Future<Output = ()>) {
        let (sender, fut) = ProtocolConnector::new(CobaltConnectedService).serve_and_log_errors();
        (Self { sender: Mutex::new(sender), phase: Mutex::new(Phase::Initial) }, fut)
    }

    /// Calculate the bucket number in the latency metric for a given duration.
    fn round_trip_time_bucket(duration: &zx::Duration) -> u32 {
        Self::cobalt_bucket(*duration, RTT_BUCKETS, RTT_BUCKET_SIZE, RTT_BUCKET_FLOOR)
    }

    /// Calculate the bucket index for a time duration. Indices follow the rules for Cobalt
    /// histograms - bucket 0 is underflow, and num_buckets + 1 is overflow.
    fn cobalt_bucket(
        duration: zx::Duration,
        num_buckets: u32,
        bucket_size: zx::Duration,
        underflow_floor: zx::Duration,
    ) -> u32 {
        let overflow_threshold = underflow_floor + (bucket_size * num_buckets);
        if duration < underflow_floor {
            0
        } else if duration > overflow_threshold {
            num_buckets + 1
        } else {
            ((duration - underflow_floor).into_nanos() / bucket_size.into_nanos()) as u32 + 1
        }
    }

    fn success(&self, sample: &HttpsSample) {
        let phase = self.phase.lock();
        let mut sender = self.sender.lock();
        sender.send(
            MetricEvent::builder(HTTPSDATE_BOUND_SIZE_MIGRATED_METRIC_ID)
                .with_event_codes(<Phase as Into<CobaltPhase>>::into(*phase).as_event_codes())
                .as_integer(sample.final_bound_size.into_micros()),
        );

        let mut bucket_counts = [0u64; RTT_BUCKETS as usize + 2];
        for bucket_idx in
            sample.polls.iter().map(|poll| Self::round_trip_time_bucket(&poll.round_trip_time))
        {
            bucket_counts[bucket_idx as usize] += 1;
        }
        let histogram_buckets = bucket_counts
            .iter()
            .enumerate()
            .filter(|(_, count)| **count > 0)
            .map(|(index, count)| HistogramBucket { index: index as u32, count: *count })
            .collect::<Vec<_>>();
        sender.send(
            MetricEvent::builder(HTTPSDATE_POLL_LATENCY_MIGRATED_METRIC_ID)
                .as_integer_histogram(histogram_buckets),
        );
    }

    fn phase_update(&self, phase: &Phase) {
        *self.phase.lock() = *phase;
    }
}

impl Diagnostics for CobaltDiagnostics {
    fn record<'a>(&self, event: Event<'a>) {
        match event {
            Event::NetworkCheckSuccessful => (),
            Event::Success(sample) => self.success(sample),
            Event::Failure(_) => (), // currently, no failures are registered with cobalt
            Event::Phase(phase) => self.phase_update(&phase),
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::datatypes::Poll,
        fidl_fuchsia_metrics::{MetricEvent, MetricEventPayload},
        futures::{channel::mpsc, stream::StreamExt},
        lazy_static::lazy_static,
        std::{collections::HashSet, iter::FromIterator},
    };

    const TEST_INITIAL_PHASE: Phase = Phase::Initial;
    const TEST_BOUND_SIZE: zx::Duration = zx::Duration::from_millis(101);
    const TEST_STANDARD_DEVIATION: zx::Duration = zx::Duration::from_millis(20);
    const ONE_MICROS: zx::Duration = zx::Duration::from_micros(1);
    const TEST_TIME: zx::Time = zx::Time::from_nanos(123_456_789);
    const TEST_RTT_BUCKET: u32 = 2;
    const TEST_RTT_2_BUCKET: u32 = 4;
    const OVERFLOW_RTT: zx::Duration = zx::Duration::from_seconds(10);
    const RTT_BUCKET_SIZE: zx::Duration =
        zx::Duration::from_micros(HTTPSDATE_POLL_LATENCY_MIGRATED_INT_BUCKETS_STEP_SIZE as i64);

    const POLL_OFFSET_RTT_BUCKET_SIZE: zx::Duration = zx::Duration::from_micros(10000);
    const POLL_OFFSET_RTT_FLOOR: zx::Duration = zx::Duration::from_micros(0);

    lazy_static! {
        static ref TEST_INITIAL_PHASE_COBALT: CobaltPhase = TEST_INITIAL_PHASE.into();
        static ref TEST_RTT: zx::Duration =
            RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * TEST_RTT_BUCKET - ONE_MICROS;
        static ref TEST_RTT_2: zx::Duration =
            RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * TEST_RTT_2_BUCKET - ONE_MICROS;
        static ref TEST_RTT_OFFSET_BUCKET: u32 = ((*TEST_RTT - POLL_OFFSET_RTT_FLOOR).into_nanos()
            / POLL_OFFSET_RTT_BUCKET_SIZE.into_nanos())
            as u32
            + 1;
        static ref TEST_RTT_2_OFFSET_BUCKET: u32 =
            ((*TEST_RTT_2 - POLL_OFFSET_RTT_FLOOR).into_nanos()
                / POLL_OFFSET_RTT_BUCKET_SIZE.into_nanos()) as u32
                + 1;
    }

    /// Create a `CobaltDiagnostics` and a receiver to inspect events it produces.
    fn diagnostics_for_test() -> (CobaltDiagnostics, mpsc::Receiver<MetricEvent>) {
        let (send, recv) = mpsc::channel(10);
        (
            CobaltDiagnostics {
                sender: Mutex::new(ProtocolSender::new(send)),
                phase: Mutex::new(TEST_INITIAL_PHASE),
            },
            recv,
        )
    }

    #[fuchsia::test]
    fn test_round_trip_time_bucket() {
        let bucket_1_rtt = RTT_BUCKET_FLOOR + ONE_MICROS;
        let bucket_5_rtt_1 = bucket_1_rtt + RTT_BUCKET_SIZE * 4;
        let overflow_rtt = RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * (RTT_BUCKETS + 2);
        let overflow_rtt_2 =
            RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * RTT_BUCKETS + zx::Duration::from_minutes(2);
        let overflow_adjacent_rtt = RTT_BUCKET_FLOOR + RTT_BUCKET_SIZE * RTT_BUCKETS - ONE_MICROS;
        let underflow_rtt = RTT_BUCKET_FLOOR - ONE_MICROS;

        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&bucket_1_rtt), 1);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&bucket_5_rtt_1), 5);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&overflow_rtt), RTT_BUCKETS + 1);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&overflow_rtt_2), RTT_BUCKETS + 1);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&overflow_adjacent_rtt), RTT_BUCKETS);
        assert_eq!(CobaltDiagnostics::round_trip_time_bucket(&underflow_rtt), 0);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_success_single_poll() {
        let (cobalt, event_recv) = diagnostics_for_test();
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![Poll::with_round_trip_time(*TEST_RTT)],
        }));
        assert_eq!(
            event_recv.take(2).collect::<Vec<_>>().await,
            vec![
                MetricEvent {
                    metric_id: HTTPSDATE_BOUND_SIZE_MIGRATED_METRIC_ID,
                    event_codes: vec![*TEST_INITIAL_PHASE_COBALT as u32],
                    payload: MetricEventPayload::IntegerValue(TEST_BOUND_SIZE.into_micros())
                },
                MetricEvent {
                    metric_id: HTTPSDATE_POLL_LATENCY_MIGRATED_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Histogram(vec![HistogramBucket {
                        index: TEST_RTT_BUCKET,
                        count: 1
                    }])
                }
            ]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_success_after_phase_update() {
        let (cobalt, mut event_recv) = diagnostics_for_test();
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![Poll::with_round_trip_time(*TEST_RTT)],
        }));
        let events = event_recv.by_ref().take(2).collect::<Vec<_>>().await;
        assert_eq!(events[0].event_codes, vec![*TEST_INITIAL_PHASE_COBALT as u32]);

        cobalt.record(Event::Phase(Phase::Converge));
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![Poll::with_round_trip_time(*TEST_RTT_2)],
        }));
        let events = event_recv.take(2).collect::<Vec<_>>().await;
        assert_eq!(events[0].event_codes, vec![CobaltPhase::Converge as u32]);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_success_multiple_rtt() {
        let (cobalt, event_recv) = diagnostics_for_test();
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![
                Poll::with_round_trip_time(*TEST_RTT),
                Poll::with_round_trip_time(*TEST_RTT_2),
                Poll::with_round_trip_time(*TEST_RTT_2),
            ],
        }));
        let mut events = event_recv.take(2).collect::<Vec<_>>().await;
        assert_eq!(
            events[0],
            MetricEvent {
                metric_id: HTTPSDATE_BOUND_SIZE_MIGRATED_METRIC_ID,
                event_codes: vec![*TEST_INITIAL_PHASE_COBALT as u32],
                payload: MetricEventPayload::IntegerValue(TEST_BOUND_SIZE.into_micros())
            }
        );
        assert_eq!(events[1].metric_id, HTTPSDATE_POLL_LATENCY_MIGRATED_METRIC_ID);
        assert!(events[1].event_codes.is_empty());
        match events.remove(1).payload {
            MetricEventPayload::Histogram(buckets) => {
                let expected_buckets: HashSet<HistogramBucket> = HashSet::from_iter(vec![
                    HistogramBucket { index: TEST_RTT_BUCKET, count: 1 },
                    HistogramBucket { index: TEST_RTT_2_BUCKET, count: 2 },
                ]);
                assert_eq!(expected_buckets, HashSet::from_iter(buckets));
            }
            p => panic!("Got unexpected payload: {:?}", p),
        }
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_success_overflow_rtt() {
        let (cobalt, event_recv) = diagnostics_for_test();
        cobalt.record(Event::Success(&HttpsSample {
            utc: TEST_TIME,
            monotonic: TEST_TIME,
            standard_deviation: TEST_STANDARD_DEVIATION,
            final_bound_size: TEST_BOUND_SIZE,
            polls: vec![Poll { round_trip_time: OVERFLOW_RTT }],
        }));
        assert_eq!(
            event_recv.take(2).collect::<Vec<_>>().await,
            vec![
                MetricEvent {
                    metric_id: HTTPSDATE_BOUND_SIZE_MIGRATED_METRIC_ID,
                    event_codes: vec![*TEST_INITIAL_PHASE_COBALT as u32],
                    payload: MetricEventPayload::IntegerValue(TEST_BOUND_SIZE.into_micros())
                },
                MetricEvent {
                    metric_id: HTTPSDATE_POLL_LATENCY_MIGRATED_METRIC_ID,
                    event_codes: vec![],
                    payload: MetricEventPayload::Histogram(vec![HistogramBucket {
                        index: RTT_BUCKETS + 1,
                        count: 1
                    }])
                },
            ]
        );
    }
}
