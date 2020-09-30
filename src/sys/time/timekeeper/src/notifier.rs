// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(unused)]

use {
    fidl_fuchsia_time as ftime, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{lock::Mutex, StreamExt as _},
    log::{info, warn},
    std::sync::Arc,
};

/// Notifies waiting clients when the clock has been updated, wrapped in a lock to allow
/// sharing between tasks.
#[derive(Clone, Debug)]
pub struct Notifier(Arc<Mutex<NotifyInner>>);

impl Notifier {
    /// Constructs a new `Notifier` initially set to the supplied source.
    pub fn new(source: ftime::UtcSource) -> Self {
        Notifier(Arc::new(Mutex::new(NotifyInner { source, clients: Vec::new() })))
    }

    /// Changes the source to the supplied value.
    pub async fn set_source(&self, source: ftime::UtcSource) {
        let monotonic = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        self.0.lock().await.set_source(source, monotonic);
    }

    /// Spawns an async task to handle requests on this channel.
    pub fn handle_request_stream(&self, requests: ftime::UtcRequestStream) {
        let notifier = self.clone();
        fasync::Task::spawn(async move {
            let mut counted_requests = requests.enumerate();
            let mut last_seen_state = notifier.0.lock().await.source;
            while let Some((request_count, Ok(ftime::UtcRequest::WatchState { responder }))) =
                counted_requests.next().await
            {
                let mut n = notifier.0.lock().await;
                // we return immediately if this is the first request on this channel, or if there
                // has been a new update since the last request.
                if request_count == 0 || last_seen_state != n.source {
                    n.reply(responder, zx::Time::get(zx::ClockId::Monotonic).into_nanos());
                } else {
                    n.register(responder);
                }
                last_seen_state = n.source;
            }
        })
        .detach();
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
    /// previous revisions, returning true iff the source changed as a result of the call.
    fn set_source(&mut self, source: ftime::UtcSource, update_time: i64) -> bool {
        if self.source != source {
            self.source = source;
            let clients = std::mem::replace(&mut self.clients, vec![]);
            info!("UTC source changed to {:?}, notifying {} clients", source, clients.len());
            for responder in clients {
                self.reply(responder, update_time);
            }
            true
        } else {
            info!("received UTC source update but the actual source didn't change.");
            false
        }
    }
}
