// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{config::Config, file_handler, inspect_fetcher::InspectFetcher},
    anyhow::Error,
    fidl_fuchsia_diagnostics_persist::{
        DataPersistenceRequest, DataPersistenceRequestStream, PersistResult,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
    log::*,
    parking_lot::Mutex,
    std::{
        collections::HashMap,
        sync::{mpsc, Arc},
    },
};

pub struct PersistServer {
    fetchers: HashMap<String, Fetcher>,
}

impl PersistServer {
    pub fn create(config: Config) -> Result<PersistServer, Error> {
        let mut persisters = HashMap::new();
        for (service_name, tags) in config.into_iter() {
            for (tag, entry) in tags.into_iter() {
                let inspect_fetcher = InspectFetcher::create(entry.selectors)?;
                let backoff = zx::Duration::from_seconds(entry.repeat_seconds);
                let fetcher =
                    Fetcher::create(inspect_fetcher, backoff, service_name.clone(), tag.clone());
                persisters.insert(tag, fetcher);
            }
        }
        Ok(PersistServer { fetchers: persisters })
    }

    // Serve the Persist FIDL protocol.
    pub fn launch_server(self, fs: &mut ServiceFs<ServiceObj<'static, ()>>) -> Result<(), Error> {
        let fetchers = self
            .fetchers
            .iter()
            .map(|(k, v)| (k.clone(), v.clone()))
            .collect::<HashMap<String, Fetcher>>();

        fs.dir("svc").add_fidl_service(move |mut stream: DataPersistenceRequestStream| {
            let mut fetchers = fetchers.clone();
            fasync::Task::spawn(async move {
                while let Ok(Some(DataPersistenceRequest::Persist { tag, responder, .. })) =
                    stream.try_next().await
                {
                    match fetchers.get_mut(&tag) {
                        None => {
                            error!("Tag '{}' was requested but is not configured", tag);
                            let _ = responder.send(PersistResult::BadName);
                        }
                        Some(fetcher) => {
                            fetcher.queue_fetch();
                            let _ = responder.send(PersistResult::Queued);
                        }
                    };
                }
            })
            .detach();
        });

        Ok(())
    }
}

#[derive(Clone)]
struct Fetcher {
    state: Arc<Mutex<FetchState>>,
    invoke_fetch: mpsc::SyncSender<()>,
    backoff: zx::Duration,
    service_name: String,
    tag: String,
}

impl Fetcher {
    fn create(
        mut source: InspectFetcher,
        backoff: zx::Duration,
        service_name: String,
        tag: String,
    ) -> Fetcher {
        // To ensure we only do one fetch-and-write at a time, put it in a task
        // triggered by a stream.
        let (invoke_fetch, receiver) = mpsc::sync_channel(0);
        let tag_copy = tag.to_string();
        let service_name_copy = service_name.to_string();
        fasync::Task::spawn(async move {
            loop {
                if let Err(_) = receiver.recv() {
                    break;
                }
                source.fetch().await.ok().map(|data| {
                    file_handler::write(&service_name_copy, &tag_copy, &data);
                });
            }
        })
        .detach();
        let fetcher = Fetcher {
            invoke_fetch,
            backoff,
            service_name: service_name.to_string(),
            tag: tag.to_string(),
            state: Arc::new(Mutex::new(FetchState::LastFetched(zx::Time::INFINITE_PAST))),
        };
        fetcher
    }

    fn queue_fetch(&mut self) {
        let mut state = self.state.lock();
        match *state {
            // If we're already scheduled to fetch, we don't have to do anything.
            FetchState::Pending => {
                info!("Fetch requested for {} but is already queued till backoff.", self.tag)
            }
            FetchState::LastFetched(last_fetched) => {
                let now = fuchsia_zircon::Time::get_monotonic();
                let earliest_fetch_allowed_time = last_fetched + self.backoff;
                // time_until_fetch_allowed will be negative if already allowed.
                let time_until_fetch_allowed = earliest_fetch_allowed_time - now;
                if time_until_fetch_allowed.into_nanos() <= 0 {
                    self.invoke_fetch.send(()).unwrap();
                    *state = FetchState::LastFetched(now);
                } else {
                    *state = FetchState::Pending;
                    // Clone a bunch of self's attributes so we dont pull it into the async task.
                    let state_for_async_task = self.state.clone();
                    let invoker_for_async_task = self.invoke_fetch.clone();
                    let service_name_for_async_task = self.service_name.clone();
                    fasync::Task::spawn(async move {
                        let mut periodic_timer =
                            fasync::Interval::new(zx::Duration::from_nanos(time_until_fetch_allowed.into_nanos() as i64,));

                        if let Some(()) = periodic_timer.next().await {
                            if let Ok(_) = invoker_for_async_task.send(()) {
                                // use earliest_fetch_allowed_time instead of now() because we want to fetch
                                // every N seconds without penalty for the time it took to get here.
                                *state_for_async_task.lock() =
                                    FetchState::LastFetched(earliest_fetch_allowed_time);
                            } else {
                                warn!("Encountered an error trying to send invoker a wakeup message for service: {}", service_name_for_async_task);
                            }
                        }
                    }).detach();
                }
            }
        }
    }
}

// FetchState stores the information needed to serve all requests with the proper backoff.
//
// When we receive a request to persist, we want to ensure that persist happens soon. But not too
// soon, to avoid disk wear and performance costs. The state handling takes some thought.
//
// There are one of three outcomes when a request is received:
// - If a fetch has not happened recently, do it now.
// - If a fetch has happened recently, ensure a request is queued to happen soon:
// - - If a request is already queued to happen soon, no additional action is needed.
// - - If no request is pending, queue one.
//
// FetchState records whether a request is currently pending or not. Immediately after a request is
// fulfilled, store the earliest time that the next request can be fulfilled.
//
// If a request arrives while state is Pending, do nothing (except log).
//
// If a request arrives before the backoff expires,
// - Set state to Pending
// - Spawn a task that will wait until the proper time, then serve the request and set state to
//    LastFetched(now).
//
// If a request arrives after backoff expires, serve it immediately and set state to
// LastFetched(now).
enum FetchState {
    Pending,
    LastFetched(zx::Time),
}

// Todo(71350): Add unit tests for backoff-time logic.
