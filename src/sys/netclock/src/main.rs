// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    chrono::{DateTime, Utc},
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
    assert_backstop_time_correct();
    diagnostics::init();
    let mut fs = ServiceFs::new();

    info!("diagnostics initialized, connecting notifier to servicefs.");
    diagnostics::INSPECTOR.export(&mut fs);

    let notifier = Notifier::new();

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
    let () = await!(fs.collect());
    Ok(())
}

fn backstop_time(path: impl AsRef<Path>) -> Result<DateTime<Utc>, Error> {
    let file_contents =
        std::fs::read_to_string(path.as_ref()).context("reading backstop time from disk")?;
    let build_time_repr = file_contents.trim();
    let parsed_offset = DateTime::parse_from_rfc3339(build_time_repr)?;
    let utc = parsed_offset.with_timezone(&Utc);
    Ok(utc)
}

fn assert_backstop_time_correct() {
    assert!(
        backstop_time("/config/build-info/latest-commit-date").unwrap() <= Utc::now(),
        "latest guaranteed UTC timestamp must be earlier than current system time"
    );
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
            await!(conn_events.try_next())
        {
            break;
        }
    }

    for i in 0.. {
        match await!(time_service.update(1)) {
            Ok(true) => {
                notifs.0.lock().set_source(
                    ftime::UtcSource::External,
                    zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                );
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
        await!(fasync::Timer::new(sleep_duration.after_now()));
    }
}

/// Notifies waiting clients when the clock has been updated, wrapped in a lock to allow
/// sharing between tasks.
#[derive(Clone, Debug)]
struct Notifier(Arc<Mutex<NotifyInner>>);

impl Notifier {
    fn new() -> Self {
        Notifier(Arc::new(Mutex::new(NotifyInner {
            source: ftime::UtcSource::Backstop,
            clients: Vec::new(),
        })))
    }

    /// Spawns an async task to handle requests on this channel.
    fn handle_request_stream(&self, requests: ftime::UtcRequestStream) {
        let notifier = self.clone();
        fasync::spawn(async move {
            info!("listening for UTC requests");
            let mut counted_requests = requests.enumerate();
            let mut last_seen_state = notifier.0.lock().source;
            while let Some((request_count, Ok(ftime::UtcRequest::WatchState { responder }))) =
                await!(counted_requests.next())
            {
                let mut n = notifier.0.lock();
                // we return immediately if this is the first request on this channel, but if
                // the backstop time hasn't been set yet then we can't say anything
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
        fuchsia_zircon as zx,
        std::{
            future::Future,
            pin::Pin,
            task::{Context, Poll, Waker},
        },
    };

    #[test]
    fn fixed_backstop_check() {
        let test_backstop = backstop_time("/pkg/data/latest-commit-date").unwrap();
        let before_test_backstop =
            Utc.from_utc_datetime(&NaiveDate::from_ymd(1999, 1, 1).and_hms(0, 0, 0));
        let after_test_backstop =
            Utc.from_utc_datetime(&NaiveDate::from_ymd(2001, 1, 1).and_hms(0, 0, 0));

        assert!(test_backstop > before_test_backstop);
        assert!(test_backstop < after_test_backstop);
    }

    #[test]
    fn current_backstop_check() {
        assert_backstop_time_correct();
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

        let notifier = Notifier::new();
        let (mut allow_update, mut wait_for_update) = futures::channel::mpsc::channel(1);
        info!("spawning test notifier");
        notifier.handle_request_stream(utc_requests);
        fasync::spawn(maintain_utc(notifier.clone(), time_service, reachability));

        fasync::spawn(async move {
            while let Some(Ok(ftz::TimeServiceRequest::Update { responder, .. })) =
                await!(time_requests.next())
            {
                let () = await!(wait_for_update.next()).unwrap();
                responder.send(true).unwrap();
            }
        });

        info!("checking that the time source has not been externally initialized yet");
        assert_eq!(await!(utc.watch_state()).unwrap().source.unwrap(), ftime::UtcSource::Backstop);

        let task_waker = await!(futures::future::poll_fn(|cx| { Poll::Ready(cx.waker().clone()) }));
        let mut cx = Context::from_waker(&task_waker);

        let mut hanging = Box::pin(utc.watch_state());
        assert!(
            hanging.as_mut().poll(&mut cx).is_pending(),
            "hanging get should not return before time updated event has been emitted"
        );

        info!("sending network update event");
        allow_update.try_send(()).unwrap();

        info!("waiting for time source update");
        assert_eq!(await!(hanging).unwrap().source.unwrap(), ftime::UtcSource::External);
    }
}
