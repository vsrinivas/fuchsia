// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `timekeeper` is responsible for external time synchronization in Fuchsia.

use {
    anyhow::{Context as _, Error},
    chrono::prelude::*,
    fidl_fuchsia_deprecatedtimezone as ftz, fidl_fuchsia_net as fnet, fidl_fuchsia_time as ftime,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
    log::{debug, error, info, warn},
    parking_lot::Mutex,
    std::sync::Arc,
};

mod diagnostics;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().context("initializing logging").unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);

    info!("retrieving UTC clock handle");
    let time_maintainer =
        fuchsia_component::client::connect_to_service::<ftime::MaintenanceMarker>().unwrap();
    let utc_clock = Arc::new(zx::Clock::from(
        time_maintainer
            .get_writable_utc_clock()
            .await
            .context("failed to get UTC clock from maintainer")?,
    ));

    diagnostics::init(Arc::clone(&utc_clock));
    let mut fs = ServiceFs::new();

    info!("diagnostics initialized, connecting notifier to servicefs.");
    diagnostics::INSPECTOR.serve(&mut fs)?;

    let source = initial_utc_source(&*utc_clock);
    let notifier = Notifier::new(source);

    info!("connecting to external update service");
    let time_service =
        fuchsia_component::client::connect_to_service::<ftz::TimeServiceMarker>().unwrap();
    let connectivity_service =
        fuchsia_component::client::connect_to_service::<fnet::ConnectivityMarker>().unwrap();

    fasync::spawn(maintain_utc(utc_clock, notifier.clone(), time_service, connectivity_service));

    fs.dir("svc").add_fidl_service(move |requests: ftime::UtcRequestStream| {
        notifier.handle_request_stream(requests);
    });

    fs.take_and_serve_directory_handle()?;
    Ok(fs.collect().await)
}

fn initial_utc_source(utc_clock: &zx::Clock) -> ftime::UtcSource {
    let clock_details = utc_clock.get_details().expect("failed to get UTC clock details");
    // When the clock is first initialized to the backstop time, its synthetic offset should
    // be identical. Once the clock is updated, this is no longer true.
    if clock_details.backstop.into_nanos() == clock_details.ticks_to_synthetic.synthetic_offset {
        ftime::UtcSource::Backstop
    } else {
        ftime::UtcSource::External
    }
}

/// The top-level control loop for time synchronization.
///
/// Checks for network connectivity before attempting any time updates.
///
/// Actual updates are performed by calls to  `fuchsia.deprecatedtimezone.TimeService` which we
/// plan to deprecate.
async fn maintain_utc(
    utc_clock: Arc<zx::Clock>,
    notifs: Notifier,
    time_service: ftz::TimeServiceProxy,
    connectivity: fnet::ConnectivityProxy,
) {
    info!("waiting for network connectivity before attempting network time sync...");
    let mut conn_events = connectivity.take_event_stream();
    loop {
        if let Ok(Some(fnet::ConnectivityEvent::OnNetworkReachable { reachable: true })) =
            conn_events.try_next().await
        {
            break;
        }
    }

    for i in 0.. {
        let sleep_duration = zx::Duration::from_seconds(2i64.pow(i)); // exponential backoff
        info!("requesting roughtime service update the system time...");
        match time_service.update(1).await {
            Ok(Some(updated_time)) => {
                let updated_time = zx::Time::from_nanos(updated_time.utc_time);
                if let Err(status) = utc_clock.update(zx::ClockUpdate::new().value(updated_time)) {
                    error!("failed to update UTC clock to time {:?}: {}", updated_time, status);
                }
                info!("adjusted UTC time to {}", Utc.timestamp_nanos(updated_time.into_nanos()));
                let monotonic_before = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
                let utc_now = Utc::now().timestamp_nanos();
                let monotonic_after = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
                info!(
                    "CF-884:monotonic_before={}:utc={}:monotonic_after={}",
                    monotonic_before, utc_now, monotonic_after,
                );
                notifs.0.lock().set_source(ftime::UtcSource::External, monotonic_before);
                break;
            }
            Ok(None) => {
                debug!(
                    "failed to update time, probably a network error. retrying in {}s.",
                    sleep_duration.into_seconds()
                );
            }
            Err(why) => {
                error!("couldn't make request to update time: {:?}", why);
            }
        }
        fasync::Timer::new(sleep_duration.after_now()).await;
    }
}

/// Notifies waiting clients when the clock has been updated, wrapped in a lock to allow
/// sharing between tasks.
#[derive(Clone, Debug)]
struct Notifier(Arc<Mutex<NotifyInner>>);

impl Notifier {
    fn new(source: ftime::UtcSource) -> Self {
        Notifier(Arc::new(Mutex::new(NotifyInner { source, clients: Vec::new() })))
    }

    /// Spawns an async task to handle requests on this channel.
    fn handle_request_stream(&self, requests: ftime::UtcRequestStream) {
        let notifier = self.clone();
        fasync::spawn(async move {
            let mut counted_requests = requests.enumerate();
            let mut last_seen_state = notifier.0.lock().source;
            while let Some((request_count, Ok(ftime::UtcRequest::WatchState { responder }))) =
                counted_requests.next().await
            {
                let mut n = notifier.0.lock();
                // we return immediately if this is the first request on this channel, or if there
                // has been a new update since the last request.
                if request_count == 0 || last_seen_state != n.source {
                    n.reply(responder, zx::Time::get(zx::ClockId::Monotonic).into_nanos());
                } else {
                    n.register(responder);
                }
                last_seen_state = n.source;
            }
        });
    }
}

/// Notifies waiting clients when the clock has been updated.
#[derive(Debug)]
struct NotifyInner {
    /// The current source for our UTC approximation.
    source: ftime::UtcSource,
    /// All clients waiting for an update to UTC's time.
    clients: Vec<ftime::UtcWatchStateResponder>,
}

impl NotifyInner {
    /// Reply to a client with the current UtcState.
    fn reply(&self, responder: ftime::UtcWatchStateResponder, update_time: i64) {
        if let Err(why) = responder
            .send(ftime::UtcState { timestamp: Some(update_time), source: Some(self.source) })
        {
            warn!("failed to notify a client of an update: {:?}", why);
        }
    }

    /// Registers a client to be later notified that a clock update has occurred.
    fn register(&mut self, responder: ftime::UtcWatchStateResponder) {
        info!("registering a client for notifications");
        self.clients.push(responder);
    }

    /// Increases the revision counter by 1 and notifies any clients waiting on updates from
    /// previous revisions.
    fn set_source(&mut self, source: ftime::UtcSource, update_time: i64) {
        if self.source != source {
            self.source = source;
            let clients = std::mem::replace(&mut self.clients, vec![]);
            info!("UTC source changed to {:?}, notifying {} clients", source, clients.len());
            for responder in clients {
                self.reply(responder, update_time);
            }
        } else {
            info!("received UTC source update but the actual source didn't change.");
        }
    }
}

#[cfg(test)]
mod tests {
    #[allow(unused)]
    use {
        super::*,
        chrono::{offset::TimeZone, NaiveDate},
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty},
        fuchsia_zircon as zx,
        matches::assert_matches,
        std::{
            future::Future,
            pin::Pin,
            task::{Context, Poll, Waker},
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn single_client() {
        // Create and start a clock.
        let clock = Arc::new(
            zx::Clock::create(zx::ClockOpts::empty(), Some(zx::Time::from_nanos(1))).unwrap(),
        );
        clock.update(zx::ClockUpdate::new().value(zx::Time::from_nanos(1))).unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        diagnostics::init(Arc::clone(&clock));
        info!("starting single notification test");

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let (time_service, mut time_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftz::TimeServiceMarker>().unwrap();
        let (reachability, reachability_server) =
            fidl::endpoints::create_proxy::<fnet::ConnectivityMarker>().unwrap();

        // the "network" the time sync server uses is de facto reachable here
        let (_, reachability_control) =
            reachability_server.into_stream_and_control_handle().unwrap();
        reachability_control.send_on_network_reachable(true).unwrap();

        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        let (mut allow_update, mut wait_for_update) = futures::channel::mpsc::channel(1);
        info!("spawning test notifier");
        notifier.handle_request_stream(utc_requests);

        fasync::spawn(maintain_utc(
            Arc::clone(&clock),
            notifier.clone(),
            time_service,
            reachability,
        ));

        fasync::spawn(async move {
            while let Some(Ok(ftz::TimeServiceRequest::Update { responder, .. })) =
                time_requests.next().await
            {
                let () = wait_for_update.next().await.unwrap();
                responder.send(Some(&mut ftz::UpdatedTime { utc_time: 1024 })).unwrap();
            }
        });

        info!("checking that the time source has not been externally initialized yet");
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::Backstop);

        info!("checking that the clock has not been updated yet");
        assert_eq!(initial_update_ticks, clock.get_details().unwrap().last_value_update_ticks);

        let task_waker = futures::future::poll_fn(|cx| Poll::Ready(cx.waker().clone())).await;
        let mut cx = Context::from_waker(&task_waker);

        let mut hanging = Box::pin(utc.watch_state());
        assert!(
            hanging.as_mut().poll(&mut cx).is_pending(),
            "hanging get should not return before time updated event has been emitted"
        );

        info!("sending network update event");
        allow_update.try_send(()).unwrap();

        info!("waiting for time source update");
        assert_eq!(hanging.await.unwrap().source.unwrap(), ftime::UtcSource::External);
        assert!(clock.get_details().unwrap().last_value_update_ticks > initial_update_ticks);
    }

    #[fasync::run_singlethreaded(test)]
    async fn initial_utc_source_initialized() {
        let clock =
            zx::Clock::create(zx::ClockOpts::empty(), Some(zx::Time::from_nanos(1_000))).unwrap();
        // The clock must be started with an initial value.
        clock.update(zx::ClockUpdate::new().value(zx::Time::from_nanos(1_000))).unwrap();
        let source = initial_utc_source(&clock);
        assert_matches!(source, ftime::UtcSource::Backstop);

        // Update the clock, which is already running.
        clock.update(zx::ClockUpdate::new().value(zx::Time::from_nanos(1_000_000))).unwrap();
        let source = initial_utc_source(&clock);
        assert_matches!(source, ftime::UtcSource::External);
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_values_are_present() -> Result<(), Error> {
        let dummy_clock = Arc::new(zx::Clock::create(zx::ClockOpts::empty(), None).unwrap());
        diagnostics::init(Arc::clone(&dummy_clock));
        assert_inspect_tree!(
            diagnostics::INSPECTOR,
            root: contains {
                start_time_monotonic_nanos: AnyProperty,
                current: contains {
                    system_uptime_monotonic_nanos: AnyProperty,
                    utc_nanos: AnyProperty,
                    utc_kernel_clock_value_nanos: AnyProperty,
                    utc_kernel_clock: contains {
                        backstop_nanos: AnyProperty,
                        "ticks_to_synthetic.reference_offset": AnyProperty,
                        "ticks_to_synthetic.synthetic_offset": AnyProperty,
                        "ticks_to_synthetic.rate.synthetic_ticks": AnyProperty,
                        "ticks_to_synthetic.rate.reference_ticks": AnyProperty,
                        "mono_to_synthetic.reference_offset": AnyProperty,
                        "mono_to_synthetic.synthetic_offset": AnyProperty,
                        "mono_to_synthetic.rate.synthetic_ticks": AnyProperty,
                        "mono_to_synthetic.rate.reference_ticks": AnyProperty,
                        error_bounds: AnyProperty,
                        query_ticks: AnyProperty,
                        last_value_update_ticks: AnyProperty,
                        last_rate_adjust_update_ticks: AnyProperty,
                        last_error_bounds_update_ticks: AnyProperty,
                        generation_counter: AnyProperty,
                    }
                }
            }
        );
        Ok(())
    }
}
