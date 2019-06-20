// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::Error,
    fidl_fuchsia_time as ftime, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::StreamExt,
    log::{debug, error, info, warn},
    parking_lot::Mutex,
    std::sync::Arc,
};

mod diagnostics;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    diagnostics::init();
    let mut fs = ServiceFs::new();

    info!("diagnostics initialized, connecting notifier to servicefs.");
    diagnostics::INSPECTOR.export(&mut fs);

    let notifier = Notifier::new();

    // TODO(CF-835): uncomment when this service can actually be connected to
    // info!("connecting to external update service");
    // let proxy =
    //     fuchsia_component::client::connect_to_service::<ftime::DeprecatedNetworkSyncMarker>()?;
    // notifier.handle_update_events(proxy);

    fs.dir("svc").add_fidl_service(move |requests: ftime::UtcRequestStream| {
        notifier.handle_request_stream(requests);
    });

    info!("added notifier, serving servicefs");
    fs.take_and_serve_directory_handle()?;
    let () = await!(fs.collect());
    Ok(())
}

/// Notifies waiting clients when the clock has been updated, wrapped in a lock to allow
/// sharing between tasks.
#[derive(Clone, Debug)]
struct Notifier(Arc<Mutex<NotifyInner>>);

impl Notifier {
    fn new() -> Self {
        Notifier(Arc::new(Mutex::new(NotifyInner {
            source: ftime::UtcSource::None,
            clients: Vec::new(),
        })))
    }

    // this code is unused in the actual binary, it'll be included when we have an external
    // implementor of DeprecatedNetworkSync to connect to in `main`
    #[cfg_attr(not(test), allow(dead_code))]
    fn handle_update_events(&self, update_events: ftime::DeprecatedNetworkSyncProxy) {
        let notifier = self.clone();
        fasync::spawn(async move {
            let mut events = update_events.take_event_stream();

            info!("listening for UTC updates from external service");
            while let Some(event) = await!(events.next()) {
                match event {
                    Ok(ftime::DeprecatedNetworkSyncEvent::UtcUpdated { update_time }) => {
                        diagnostics::utc_updated(update_time);
                        notifier.0.lock().set_source(ftime::UtcSource::External, update_time);
                    }
                    Err(why) => error!("problem receiving utc updated notif: {:?}", why),
                }
            }
        });
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
            debug!("UTC source changed, notifying {} clients", clients.len());
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
        fuchsia_zircon as zx,
        std::{
            future::Future,
            pin::Pin,
            task::{Context, Poll, Waker},
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn single_client() {
        diagnostics::init();
        info!("starting single notification test");

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let (depsync, depsync_server) =
            fidl::endpoints::create_proxy::<ftime::DeprecatedNetworkSyncMarker>().unwrap();

        let (_, depsync_control) = depsync_server.into_stream_and_control_handle().unwrap();

        let notifier = Notifier::new();
        info!("spawning test notifier");
        notifier.handle_request_stream(utc_requests);
        notifier.handle_update_events(depsync);

        info!("checking that the time source has not been initialized yet");
        assert_eq!(await!(utc.watch_state()).unwrap().source.unwrap(), ftime::UtcSource::None);

        let task_waker = await!(futures::future::poll_fn(|cx| { Poll::Ready(cx.waker().clone()) }));
        let mut cx = Context::from_waker(&task_waker);

        let mut hanging = Box::pin(utc.watch_state());
        assert!(
            hanging.as_mut().poll(&mut cx).is_pending(),
            "hanging get should not return before time updated event has been emitted"
        );

        info!("sending network update event");
        depsync_control
            .send_utc_updated(zx::Time::get(zx::ClockId::Monotonic).into_nanos())
            .unwrap();

        info!("waiting for time source update");
        assert_eq!(await!(hanging).unwrap().source.unwrap(), ftime::UtcSource::External);
    }
}
