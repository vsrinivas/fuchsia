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
    parking_lot::Mutex,
    serde_json::{self, json, map::Entry, Map, Value},
    std::{collections::HashMap, sync::Arc},
    thiserror::Error,
    tracing::*,
};

// The capability name for the Inspect reader
const INSPECT_SERVICE_PATH: &str = "/svc/fuchsia.diagnostics.FeedbackArchiveAccessor";

// Keys for JSON per-tag metadata to be persisted and published
const TIMESTAMPS_KEY: &str = "@timestamps";
const ERROR_KEY: &str = ":error";
const ERROR_DESCRIPTION_KEY: &str = "description";

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
            let backoff = zx::Duration::from_seconds(entry.min_seconds_between_fetch);
            let fetcher = Fetcher::new(FetcherArgs {
                source: inspect_fetcher,
                backoff,
                max_save_length: entry.max_bytes,
                service_name: service_name.clone(),
                tag: tag.clone(),
            });
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
            format!("{}-{}", constants::PERSIST_SERVICE_NAME_PREFIX, self.service_name);

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
                                warn!("Tag '{}' was requested but is not configured", tag);
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

#[derive(serde::Serialize)]
struct Timestamps {
    before_monotonic: i64,
    before_utc: i64,
    after_monotonic: i64,
    after_utc: i64,
}

fn value_type_name(value: &Value) -> &'static str {
    match value {
        Value::Array(_) => "array",
        Value::Null => "null",
        Value::Bool(_) => "bool",
        Value::Number(_) => "number",
        Value::String(_) => "string",
        Value::Object(_) => "object",
    }
}

#[derive(Debug, Error)]
enum FoldError {
    #[error("Unable to merge existing {existing} entry with retrieved {retrieved} payload")]
    MergeError { existing: &'static str, retrieved: &'static str },
    #[error("Moniker {0:?} wasn't string")]
    MonikerNotString(Value),
}

fn string_to_save(inspect_data: &str, timestamps: &Timestamps, max_save_length: usize) -> String {
    fn compose_error(timestamps: &Timestamps, description: String) -> Value {
        json!({
            TIMESTAMPS_KEY: timestamps,
            ERROR_KEY: {
                ERROR_DESCRIPTION_KEY: description
            }
        })
    }

    let json_inspect: Value = serde_json::from_str(&inspect_data).expect("parsing json failed.");
    let json_timestamps = json!(timestamps);
    let timestamps_length = TIMESTAMPS_KEY.len() + json_timestamps.to_string().len() + 4;
    let mut save_string = match json_inspect {
        Value::Array(items) => {
            let entries = items.into_iter().try_fold(Map::new(), |mut entries, mut item| {
                let moniker = item["moniker"].take();
                let payload = item["payload"]["root"].take();
                match moniker {
                    Value::String(moniker) => match entries.entry(moniker) {
                        Entry::Occupied(mut o) => match (o.get_mut(), payload) {
                            (Value::Object(root_map), Value::Object(payload)) => {
                                root_map.extend(payload)
                            }
                            // Merging in Null is a no-op.
                            (_, Value::Null) => (),
                            // Replace Null values with new values.
                            (map_value @ Value::Null, payload) => {
                                *map_value = payload;
                            }
                            (obj, payload) => {
                                return Err(FoldError::MergeError {
                                    existing: value_type_name(&obj),
                                    retrieved: value_type_name(&payload),
                                });
                            }
                        },
                        Entry::Vacant(v) => {
                            let _ = v.insert(payload);
                        }
                    },
                    bad_moniker => return Err(FoldError::MonikerNotString(bad_moniker)),
                };
                Ok(entries)
            });
            match entries {
                Ok(mut entries) => {
                    entries.insert(TIMESTAMPS_KEY.to_string(), json_timestamps);
                    Value::Object(entries)
                }
                Err(e) => {
                    let msg = format!("Fold error: {}", e);
                    compose_error(timestamps, msg)
                }
            }
        }
        _ => {
            error!("Inspect wasn't an array");
            compose_error(timestamps, "Internal error: Inspect wasn't an array".to_string())
        }
    }
    .to_string();
    let data_length = save_string.len() - timestamps_length;
    if data_length > max_save_length {
        let error_description =
            format!("Data too big: {} > max length {}", data_length, max_save_length,);
        save_string = compose_error(timestamps, error_description).to_string();
    }
    save_string
}

struct FetcherArgs {
    source: InspectFetcher,
    backoff: zx::Duration,
    max_save_length: usize,
    service_name: String,
    tag: String,
}

fn utc_now() -> i64 {
    let now_utc = chrono::prelude::Utc::now(); // Consider using SystemTime::now()?
    now_utc.timestamp() * 1_000_000_000 + now_utc.timestamp_subsec_nanos() as i64
}

impl Fetcher {
    fn new(args: FetcherArgs) -> Self {
        // To ensure we only do one fetch-and-write at a time, put it in a task
        // triggered by a stream.
        let FetcherArgs { tag, service_name, max_save_length, backoff, mut source } = args;
        let (invoke_fetch, mut receiver) = mpsc::channel::<()>(0);
        let tag_copy = tag.to_string();
        let service_name_copy = service_name.to_string();
        fasync::Task::spawn(async move {
            loop {
                if let Some(_) = receiver.next().await {
                    let before_utc = utc_now();
                    let before_monotonic = zx::Time::get_monotonic().into_nanos();
                    source.fetch().await.ok().map(|data| {
                        let after_utc = utc_now();
                        let after_monotonic = zx::Time::get_monotonic().into_nanos();
                        let timestamps =
                            Timestamps { before_utc, before_monotonic, after_utc, after_monotonic };
                        let save_string = string_to_save(&data, &timestamps, max_save_length);
                        file_handler::write(&service_name_copy, &tag_copy, &save_string);
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

#[cfg(test)]
mod tests {
    use test_case::test_case;

    use super::*;

    const TIMESTAMPS: Timestamps =
        Timestamps { after_monotonic: 200, after_utc: 111, before_monotonic: 100, before_utc: 110 };

    const MAX_SAVE_LENGTH: usize = 1000;

    #[test_case(false; "null second")]
    #[test_case(true; "null first")]
    fn string_to_save_ignores_null_payload(permute: bool) {
        let mut inspect_data = [
            json!({
                "moniker": "core/fake-moniker",
                "payload": {
                    "root": {
                        "some/inspect/path": 55,
                    }
                }
            }),
            json!(
                {
                    "moniker": "core/fake-moniker",
                    "payload": {
                        "root": null,
                    }
                }
            ),
        ];
        if permute {
            inspect_data.reverse();
        }
        let inspect_data = Value::Array(Vec::from(inspect_data));
        let str = string_to_save(&inspect_data.to_string(), &TIMESTAMPS, MAX_SAVE_LENGTH);
        assert_eq!(
            serde_json::from_str::<'_, Value>(&str).unwrap(),
            json!({
                "@timestamps": {
                    "after_monotonic": 200,
                    "after_utc": 111,
                    "before_monotonic": 100,
                    "before_utc": 110,
                },
                "core/fake-moniker": {"some/inspect/path": 55},
            })
        );
    }

    #[test]
    fn string_to_save_fails_heterogenous_payload() {
        let inspect_data = json!([
            {
                "moniker": "core/fake-moniker",
                "payload": {
                    "root": {
                        "some/inspect/path": 55,
                    }
                }
            },
            {
                "moniker": "core/fake-moniker",
                "payload": {
                    "root": 32,
                }
            }
        ]);
        let str = string_to_save(&inspect_data.to_string(), &TIMESTAMPS, MAX_SAVE_LENGTH);
        let values = serde_json::from_str::<'_, Map<_, _>>(&str).unwrap();
        let message = values[":error"]["description"].as_str().unwrap();
        assert!(message.contains("Unable to merge"));
    }

    #[test]
    fn string_to_save_merges_data() {
        let inspect_data = json!([
            {
                "moniker": "core/fake-moniker",
                "payload": {
                    "root": {
                        "some/inspect/path": 1,
                        "a/duplicate/path": 2,
                    }
                }
            },
            {
                "moniker": "core/fake-moniker",
                "payload": {
                    "root": {
                        "a/duplicate/path": 3,
                        "other/inspect/path": 4,
                    }
                }
            }
        ]);
        let str = string_to_save(&inspect_data.to_string(), &TIMESTAMPS, MAX_SAVE_LENGTH);
        assert_eq!(
            serde_json::from_str::<'_, Value>(&str).unwrap(),
            json!({
                "@timestamps": {
                    "after_monotonic": 200,
                    "after_utc": 111,
                    "before_monotonic": 100,
                    "before_utc": 110,
                },
                "core/fake-moniker": {
                    "some/inspect/path": 1,
                    "a/duplicate/path": 3,
                    "other/inspect/path": 4
                },
            })
        );
    }
}
