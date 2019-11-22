// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `timekeeper` is responsible for external time synchronization in Fuchsia.

use {
    chrono::prelude::*,
    failure::{Error, ResultExt},
    fidl_fuchsia_net as fnet, fidl_fuchsia_time as ftime, fidl_fuchsia_timezone as ftz,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
    log::{debug, error, info, warn},
    parking_lot::Mutex,
    std::{path::Path, sync::Arc},
};

mod diagnostics;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    diagnostics::init();
    let mut fs = ServiceFs::new();

    info!("diagnostics initialized, connecting notifier to servicefs.");
    diagnostics::INSPECTOR.serve(&mut fs)?;

    let source = initial_utc_source("/config/build-info/minimum-utc-stamp".as_ref())?;
    let notifier = Notifier::new(source);

    info!("connecting to external update service");
    let time_service =
        fuchsia_component::client::connect_to_service::<ftz::TimeServiceMarker>().unwrap();
    let connectivity_service =
        fuchsia_component::client::connect_to_service::<fnet::ConnectivityMarker>().unwrap();

    fasync::spawn(maintain_utc(notifier.clone(), time_service, connectivity_service));

    fs.dir("svc").add_fidl_service(move |requests: ftime::UtcRequestStream| {
        notifier.handle_request_stream(requests);
    });

    info!("added notifier, serving servicefs");
    fs.take_and_serve_directory_handle()?;
    let () = fs.collect().await;
    Ok(())
}

fn backstop_time(path: &Path) -> Result<DateTime<Utc>, Error> {
    let file_contents = std::fs::read_to_string(path).context("reading backstop time from disk")?;
    let parsed_offset = NaiveDateTime::parse_from_str(file_contents.trim(), "%s")?;
    let utc = DateTime::from_utc(parsed_offset, Utc);
    Ok(utc)
}

fn initial_utc_source(backstop_path: &Path) -> Result<Option<ftime::UtcSource>, Error> {
    let expected_minimum = backstop_time(backstop_path)?;
    let current_utc = Utc::now();
    Ok(if current_utc > expected_minimum {
        Some(ftime::UtcSource::Backstop)
    } else {
        warn!(
            "latest known-past UTC time ({}) should be earlier than current system time ({})",
            expected_minimum, current_utc,
        );
        None
    })
}

/// The top-level control loop for time synchronization.
///
/// Checks for network connectivity before attempting any time updates.
///
/// Actual updates are performed by calls to  `fuchsia.timezone.TimeService` which we plan
/// to deprecate.
async fn maintain_utc(
    notifs: Notifier,
    time_service: ftz::TimeServiceProxy,
    connectivity: fnet::ConnectivityProxy,
) {
    // wait for the network to come up before we start checking for time
    let mut conn_events = connectivity.take_event_stream();
    loop {
        if let Ok(Some(fnet::ConnectivityEvent::OnNetworkReachable { reachable: true })) =
            conn_events.try_next().await
        {
            break;
        }
    }

    for i in 0.. {
        match time_service.update(1).await {
            Ok(true) => {
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
            Ok(false) => {
                debug!("failed to update time, probably a network error. retrying.");
            }
            Err(why) => {
                error!("couldn't make request to update time: {:?}", why);
            }
        }
        let sleep_duration = zx::Duration::from_seconds(2i64.pow(i)); // exponential backoff
        fasync::Timer::new(sleep_duration.after_now()).await;
    }
}

/// Notifies waiting clients when the clock has been updated, wrapped in a lock to allow
/// sharing between tasks.
#[derive(Clone, Debug)]
struct Notifier(Arc<Mutex<NotifyInner>>);

impl Notifier {
    fn new(source: Option<ftime::UtcSource>) -> Self {
        Notifier(Arc::new(Mutex::new(NotifyInner { source, clients: Vec::new() })))
    }

    /// Spawns an async task to handle requests on this channel.
    fn handle_request_stream(&self, requests: ftime::UtcRequestStream) {
        let notifier = self.clone();
        fasync::spawn(async move {
            info!("listening for UTC requests");
            let mut counted_requests = requests.enumerate();
            let mut last_seen_state = notifier.0.lock().source;
            while let Some((request_count, Ok(ftime::UtcRequest::WatchState { responder }))) =
                counted_requests.next().await
            {
                let mut n = notifier.0.lock();
                // we return immediately if this is the first request on this channel, but if
                // the backstop time hasn't been set yet then we can't say anything
                if n.source.is_some() && (request_count == 0 || last_seen_state != n.source) {
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
    source: Option<ftime::UtcSource>,
    /// All clients waiting for an update to UTC's time.
    clients: Vec<ftime::UtcWatchStateResponder>,
}

impl NotifyInner {
    /// Reply to a client with the current UtcState.
    fn reply(&self, responder: ftime::UtcWatchStateResponder, update_time: i64) {
        if let Err(why) =
            responder.send(ftime::UtcState { timestamp: Some(update_time), source: self.source })
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
        if self.source != Some(source) {
            self.source = Some(source);
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
        fuchsia_zircon as zx,
        std::{
            future::Future,
            pin::Pin,
            task::{Context, Poll, Waker},
        },
    };

    #[test]
    fn fixed_backstop_check() {
        let y2k_backstop = "/pkg/data/y2k";
        let test_backstop = backstop_time(y2k_backstop.as_ref()).unwrap();
        let test_source = initial_utc_source(y2k_backstop.as_ref()).unwrap();
        let before_test_backstop =
            Utc.from_utc_datetime(&NaiveDate::from_ymd(1999, 1, 1).and_hms(0, 0, 0));
        let after_test_backstop =
            Utc.from_utc_datetime(&NaiveDate::from_ymd(2001, 1, 1).and_hms(0, 0, 0));

        assert!(test_backstop > before_test_backstop);
        assert!(test_backstop < after_test_backstop);
        assert_eq!(test_source, Some(ftime::UtcSource::Backstop));
    }

    #[test]
    fn fallible_backstop_check() {
        assert_eq!(initial_utc_source("/pkg/data/end-of-unix-time".as_ref()).unwrap(), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn single_client() {
        diagnostics::init();
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

        let notifier = Notifier::new(Some(ftime::UtcSource::Backstop));
        let (mut allow_update, mut wait_for_update) = futures::channel::mpsc::channel(1);
        info!("spawning test notifier");
        notifier.handle_request_stream(utc_requests);
        fasync::spawn(maintain_utc(notifier.clone(), time_service, reachability));

        fasync::spawn(async move {
            while let Some(Ok(ftz::TimeServiceRequest::Update { responder, .. })) =
                time_requests.next().await
            {
                let () = wait_for_update.next().await.unwrap();
                responder.send(true).unwrap();
            }
        });

        info!("checking that the time source has not been externally initialized yet");
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::Backstop);

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
    }
}
