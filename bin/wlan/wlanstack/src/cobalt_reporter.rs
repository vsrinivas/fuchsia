// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fdio;
use fidl_fuchsia_cobalt::{HistogramBucket, LoggerFactoryMarker, LoggerProxy,
                          ProjectProfile, ReleaseStage, Status};
use fidl_fuchsia_mem as fuchsia_mem;
use futures::channel::mpsc;
use futures::prelude::*;
use futures::StreamExt;
use log::{error, info};
use std::fs::File;
use std::io::Seek;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

const COBALT_CONFIG_PATH: &'static str = "/pkg/data/cobalt_config.pb";

const COBALT_BUFFER_SIZE: usize = 100;

enum EventValue {
    Count {
        event_type_index: u32,
        count: i64,
    },
    ElapsedTime {
        event_type_index: u32,
        elapsed_micros: i64,
    },
    IntHistogram {
        values: Vec<HistogramBucket>,
    },
}

struct Event {
    metric_id: u32,
    value: EventValue,
}

#[derive(Clone)]
pub struct CobaltSender {
    sender: mpsc::Sender<Event>,
    is_blocked: Arc<AtomicBool>,
}

impl CobaltSender {
    pub fn log_event_count(&mut self, metric_id: u32, event_type_index: u32, count: i64) {
        let event_value = EventValue::Count {
            event_type_index,
            count,
        };
        self.log_event(metric_id, event_value);
    }

    pub fn log_elapsed_time(&mut self, metric_id: u32, event_type_index: u32, elapsed_micros: i64) {
        let event_value = EventValue::ElapsedTime {
            event_type_index,
            elapsed_micros,
        };
        self.log_event(metric_id, event_value);
    }

    pub fn log_int_histogram(&mut self, metric_id: u32, values: Vec<HistogramBucket>) {
        let event_value = EventValue::IntHistogram { values };
        self.log_event(metric_id, event_value);
    }

    fn log_event(&mut self, metric_id: u32, value: EventValue) {
        let event = Event { metric_id, value };
        if self.sender.try_send(event).is_err() {
            let was_blocked = self.is_blocked.compare_and_swap(false, true, Ordering::SeqCst);
            if !was_blocked {
                error!("cobalt sender drops a event/events: either buffer is full or no receiver is waiting");
            }
        } else {
            let was_blocked = self.is_blocked.compare_and_swap(true, false, Ordering::SeqCst);
            if was_blocked {
                info!("cobalt sender recovers and resumes sending")
            }

        }
    }
}

pub fn serve() -> (CobaltSender, impl Future<Output = ()>) {
    let (sender, receiver) = mpsc::channel(COBALT_BUFFER_SIZE);
    let sender = CobaltSender { sender, is_blocked: Arc::new(AtomicBool::new(false)) };
    let fut = send_cobalt_events(receiver);
    (sender, fut)
}

async fn get_cobalt_logger() -> Result<LoggerProxy, Error> {
    let (logger_proxy, server_end) =
        fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
    let logger_factory = fuchsia_app::client::connect_to_service::<LoggerFactoryMarker>()
        .context("Failed to connect to the Cobalt LoggerFactory")?;

    let mut cobalt_config = File::open(COBALT_CONFIG_PATH)?;
    let vmo = fdio::get_vmo_copy_from_file(&cobalt_config)?;
    let size = cobalt_config.seek(std::io::SeekFrom::End(0))?;

    let config = fuchsia_mem::Buffer { vmo, size };

    let res = await!(logger_factory.create_logger(
        &mut ProjectProfile {
            config,
            release_stage: ReleaseStage::Ga,
        },
        server_end,
    ));
    handle_cobalt_factory_result(res, "Failed to obtain Logger")?;
    Ok(logger_proxy)
}

fn handle_cobalt_factory_result(
    r: Result<Status, fidl::Error>, context: &str,
) -> Result<(), failure::Error> {
    match r {
        Ok(Status::Ok) => Ok(()),
        Ok(other) => Err(format_err!("{}: {:?}", context, other)),
        Err(e) => Err(format_err!("{}: {}", context, e)),
    }
}

async fn send_cobalt_events(mut receiver: mpsc::Receiver<Event>) {
    let logger = match await!(get_cobalt_logger()) {
        Ok(logger) => logger,
        Err(e) => {
            error!("Error obtaining a Cobalt Logger: {}", e);
            return;
        }
    };

    let mut is_full = false;
    while let Some(event) = await!(receiver.next()) {
        match event.value {
            EventValue::Count {
                event_type_index,
                count,
            } => {
                let resp = await!(logger.log_event_count(
                    event.metric_id,
                    event_type_index,
                    "",
                    0, // TODO report a period duration once the backend supports it.
                    count
                ));
                handle_cobalt_response(resp, event.metric_id, &mut is_full);
            }
            EventValue::ElapsedTime {
                event_type_index,
                elapsed_micros,
            } => {
                let resp = await!(logger.log_elapsed_time(
                    event.metric_id,
                    event_type_index,
                    "",
                    elapsed_micros
                ));
                handle_cobalt_response(resp, event.metric_id, &mut is_full);
            }
            EventValue::IntHistogram { mut values } => {
                let resp = await!(logger.log_int_histogram(
                    event.metric_id,
                    0,
                    "",
                    &mut values.iter_mut()
                ));
                handle_cobalt_response(resp, event.metric_id, &mut is_full);
            }
        }
    }
}

fn handle_cobalt_response(resp: Result<Status, fidl::Error>, metric_id: u32, is_full: &mut bool) {
    if let Err(e) = throttle_cobalt_error(resp, metric_id, is_full) {
        error!("{}", e);
    }
}

fn throttle_cobalt_error(
    resp: Result<Status, fidl::Error>, metric_id: u32, is_full: &mut bool,
) -> Result<(), failure::Error> {
    let was_full = *is_full;
    *is_full = resp.as_ref().ok() == Some(&Status::BufferFull);
    match resp {
        Ok(Status::BufferFull) => {
            if !was_full {
                Err(format_err!(
                    "Cobalt buffer became full. Cannot report the stats"
                ))
            } else {
                Ok(())
            }
        }
        Ok(Status::Ok) => Ok(()),
        Ok(other) => Err(format_err!(
            "Cobalt returned an error for metric {}: {:?}",
            metric_id,
            other
        )),
        Err(e) => Err(format_err!(
            "Failed to send event to Cobalt for metric {}: {}",
            metric_id,
            e
        )),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_cobalt::Status;

    #[test]
    fn throttle_errors() {
        let mut is_full = false;

        let cobalt_resp = Ok(Status::Ok);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, false);

        let cobalt_resp = Ok(Status::InvalidArguments);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, false);

        let cobalt_resp = Ok(Status::BufferFull);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, true);

        let cobalt_resp = Ok(Status::BufferFull);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, true);

        let cobalt_resp = Ok(Status::Ok);
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, false);

        let cobalt_resp = Err(fidl::Error::ClientWrite(
            fuchsia_zircon::Status::PEER_CLOSED,
        ));
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, false);
    }
}
