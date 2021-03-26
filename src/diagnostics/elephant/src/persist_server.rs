// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{config::TaggedPersist, constants, file_handler},
    anyhow::Error,
    fidl_fuchsia_diagnostics_persist::{
        DataPersistenceRequest, DataPersistenceRequestStream, PersistResult,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_zircon as zx,
    futures::{channel::mpsc, SinkExt, StreamExt},
    inspect_fetcher::InspectFetcher,
    log::*,
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc},
};

// The capability name for the Inspect reader
const INSPECT_SERVICE_PATH: &str = "/svc/fuchsia.diagnostics.FeedbackArchiveAccessor";

pub struct PersistServer {
    // Service name that this persist server is hosting.
    service_name: String,
    // Mapping from a string tag to an archive reader
    // configured to fetch a specific set of selectors.
    fetchers: HashMap<String, Fetcher>,
}

impl PersistServer {
    pub fn create(
        service_name: String,
        tags: HashMap<String, TaggedPersist>,
    ) -> Result<PersistServer, Error> {
        let mut persisters = HashMap::new();
        for (tag, entry) in tags.into_iter() {
            let inspect_fetcher = InspectFetcher::create(INSPECT_SERVICE_PATH, entry.selectors)?;
            let backoff = zx::Duration::from_seconds(entry.repeat_seconds);
            let fetcher =
                Fetcher::create(inspect_fetcher, backoff, service_name.clone(), tag.clone());
            persisters.insert(tag, fetcher);
        }
        Ok(PersistServer { service_name, fetchers: persisters })
    }

    // Serve the Persist FIDL protocol.
    pub fn launch_server(self, fs: &mut ServiceFs<ServiceObj<'static, ()>>) -> Result<(), Error> {
        let fetchers = self
            .fetchers
            .iter()
            .map(|(k, v)| (k.clone(), v.clone()))
            .collect::<HashMap<String, Fetcher>>();

        let unique_service_name =
            format!("{}_{}", constants::PERSIST_SERVICE_NAME_PREFIX, self.service_name);

        fs.dir("svc").add_fidl_service_at(
            unique_service_name,
            move |mut stream: DataPersistenceRequestStream| {
                let mut fetchers = fetchers.clone();
                fasync::Task::spawn(async move {
                    while let Some(Ok(DataPersistenceRequest::Persist { tag, responder, .. })) =
                        stream.next().await
                    {
                        match fetchers.get_mut(&tag) {
                            None => {
                                error!("Tag '{}' was requested but is not configured", tag);
                                let _ = responder.send(PersistResult::BadName);
                            }
                            Some(fetcher) => {
                                match fetcher.queue_fetch().await {
                                    Ok(_) => {
                                        responder.send(PersistResult::Queued).unwrap_or_else(|err| warn!("Failed to notify client that work was queued: {}", err));
                                    }
                                    Err(e) => {
                                        fetchers.remove(&tag);
                                        warn!("Fetcher removed because queuing tasks is now failing: {:?}", e);
                                    }
                                }
                            }
                        };
                    }
                })
                .detach();
            },
        );

        Ok(())
    }
}

#[derive(Clone)]
struct Fetcher {
    state: Arc<Mutex<FetchState>>,
    invoke_fetch: mpsc::Sender<()>,
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
        let (invoke_fetch, mut receiver) = mpsc::channel::<()>(0);
        let tag_copy = tag.to_string();
        let service_name_copy = service_name.to_string();
        fasync::Task::spawn(async move {
            loop {
                if let Some(_) = receiver.next().await {
                    source.fetch().await.ok().map(|data| {
                        file_handler::write(&service_name_copy, &tag_copy, &data);
                    });
                } else {
                    break;
                }
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

    async fn queue_fetch(&mut self) -> Result<(), Error> {
        let queue_task: Option<zx::Time> = {
            let mut state = self.state.lock();
            if let FetchState::LastFetched(x) = *state {
                let last_fetched = x;
                *state = FetchState::Pending;
                Some(last_fetched)
            } else {
                None
            }
        };

        match queue_task {
            // If we're already scheduled to fetch, we don't have to do anything.
            None => {
                info!("Fetch requested for {} but is already queued till backoff.", self.tag);
                Ok(())
            }
            Some(last_fetched) => {
                let now = fuchsia_zircon::Time::get_monotonic();
                let earliest_fetch_allowed_time = last_fetched + self.backoff;

                if earliest_fetch_allowed_time < now {
                    self.invoke_fetch.send(()).await?;
                    *self.state.lock() = FetchState::LastFetched(now);
                    Ok(())
                } else {
                    *self.state.lock() = FetchState::Pending;
                    // Clone a bunch of self's attributes so we dont pull it into the async task.
                    let state_for_async_task = self.state.clone();
                    let mut invoker_for_async_task = self.invoke_fetch.clone();
                    let service_name_for_async_task = self.service_name.clone();

                    // Calculate only if we know earliest time allowed is greater than now to avoid overflows.
                    let time_until_fetch_allowed = earliest_fetch_allowed_time - now;

                    fasync::Task::spawn(async move {
                        let mut periodic_timer =
                            fasync::Interval::new(zx::Duration::from_nanos(time_until_fetch_allowed.into_nanos() as i64,));

                        if let Some(()) = periodic_timer.next().await {
                            if let Ok(_) = invoker_for_async_task.send(()).await {
                                // use earliest_fetch_allowed_time instead of now() because we want to fetch
                                // every N seconds without penalty for the time it took to get here.
                                *state_for_async_task.lock() =
                                    FetchState::LastFetched(earliest_fetch_allowed_time);
                            } else {
                                // TODO(cphoenix): We need to remove this dead fetcher from the persist service, but the existing
                                // fire-forget architecture causes us to lose communication at this point.
                                warn!("Encountered an error trying to send invoker a wakeup message for service: {}", service_name_for_async_task);
                            }
                        }
                    }).detach();
                    Ok(())
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
