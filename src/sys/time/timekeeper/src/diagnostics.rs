#[cfg(test)]
use fidl_fuchsia_cobalt::CobaltEvent;
use {
    fuchsia_async as fasync,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_inspect::{Inspector, IntProperty, Property},
    fuchsia_zircon as zx,
    futures::FutureExt,
    lazy_static::lazy_static,
    std::sync::Arc,
};

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
    static ref START_TIME_MONO: IntProperty =
        INSPECTOR.root().create_int("start_time_monotonic_nanos", 0);
}

fn monotonic_time() -> i64 {
    zx::Time::get(zx::ClockId::Monotonic).into_nanos()
}

fn utc_time() -> i64 {
    zx::Time::get(zx::ClockId::UTC).into_nanos()
}

pub fn init(utc_clock: Arc<zx::Clock>) {
    START_TIME_MONO.set(monotonic_time());
    INSPECTOR.root().record_lazy_child("current", move || {
        let utc_clock_clone = Arc::clone(&utc_clock);
        async move {
            let inspector = Inspector::new();
            let root = inspector.root();
            root.record_int("system_uptime_monotonic_nanos", monotonic_time());
            root.record_int("utc_nanos", utc_time());
            root.record_int(
                "utc_kernel_clock_value_nanos",
                utc_clock_clone.read().map(zx::Time::into_nanos).unwrap_or(0),
            );

            if let Ok(details) = utc_clock_clone.get_details() {
                let child = root.create_child("utc_kernel_clock");
                child.record_int("backstop_nanos", details.backstop.into_nanos());
                child.record_int(
                    "ticks_to_synthetic.reference_offset",
                    details.ticks_to_synthetic.reference_offset,
                );
                child.record_int(
                    "ticks_to_synthetic.synthetic_offset",
                    details.ticks_to_synthetic.synthetic_offset,
                );
                child.record_int(
                    "ticks_to_synthetic.rate.synthetic_ticks",
                    details.ticks_to_synthetic.rate.synthetic_ticks as i64,
                );
                child.record_int(
                    "ticks_to_synthetic.rate.reference_ticks",
                    details.ticks_to_synthetic.rate.reference_ticks as i64,
                );
                child.record_int(
                    "mono_to_synthetic.reference_offset",
                    details.mono_to_synthetic.reference_offset,
                );
                child.record_int(
                    "mono_to_synthetic.synthetic_offset",
                    details.mono_to_synthetic.synthetic_offset,
                );
                child.record_int(
                    "mono_to_synthetic.rate.synthetic_ticks",
                    details.mono_to_synthetic.rate.synthetic_ticks as i64,
                );
                child.record_int(
                    "mono_to_synthetic.rate.reference_ticks",
                    details.mono_to_synthetic.rate.reference_ticks as i64,
                );
                child.record_uint("error_bounds", details.error_bounds);
                child.record_int("query_ticks", details.query_ticks);
                child.record_int("last_value_update_ticks", details.last_value_update_ticks);
                child.record_int(
                    "last_rate_adjust_update_ticks",
                    details.last_rate_adjust_update_ticks,
                );
                child.record_int(
                    "last_error_bounds_update_ticks",
                    details.last_error_bounds_update_ticks,
                );
                child.record_int("generation_counter", details.generation_counter as i64);
                root.record(child);
            }
            Ok(inspector)
        }
        .boxed()
    });
}

/// A connection to the Cobalt service providing convenience functions for logging time metrics.
pub struct CobaltMetrics {
    /// The wrapped CobaltSender used to log metrics.
    sender: CobaltSender,
    // TODO(57677): Move back to an owned fasync::Task instead of detaching the spawned Task
    // once the lifecycle of timekeeper ensures CobaltMetrics objects will last long enough
    // to finish their logging.
}

impl CobaltMetrics {
    /// Contructs a new CobaltMetrics instance.
    pub fn new() -> Self {
        let (sender, fut) = CobaltConnector::default()
            .serve(ConnectionType::project_id(time_metrics_registry::PROJECT_ID));
        fasync::Task::spawn(fut).detach();
        Self { sender }
    }

    #[cfg(test)]
    /// Construct a mock CobaltMetrics for use in unit tests, returning the CobaltMetrics object
    /// and an mpsc Receiver that receives any logged metrics.
    // TODO(jsankey): As design evolves consider defining CobaltMetrics as a trait with one
    //                implementation for production and a second mock implementation for unittest
    //                with support for ergonomic assertions.
    pub fn new_mock() -> (Self, futures::channel::mpsc::Receiver<CobaltEvent>) {
        let (mpsc_sender, mpsc_receiver) = futures::channel::mpsc::channel(1);
        let sender = CobaltSender::new(mpsc_sender);
        (CobaltMetrics { sender }, mpsc_receiver)
    }

    /// Records a Timekeeper lifecycle event.
    pub fn log_lifecycle_event(
        &mut self,
        event_type: time_metrics_registry::TimeMetricDimensionEventType,
    ) {
        self.sender
            .log_event(time_metrics_registry::TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID, event_type)
    }
}
