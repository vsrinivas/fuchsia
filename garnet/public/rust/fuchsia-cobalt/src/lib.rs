// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{bail, Error, ResultExt},
    fdio, fidl,
    fidl_fuchsia_cobalt::{
        HistogramBucket, LoggerFactoryMarker, LoggerProxy, ProjectProfile,
        ReleaseStage, Status,
    },
    fidl_fuchsia_mem as fuchsia_mem,
    futures::{channel::mpsc, prelude::*, StreamExt},
    log::{error, info},
    std::{
        fs::File,
        io::Seek,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
};

enum EventValue {
    Event {
        event_code: u32,
    },
    Count {
        event_code: u32,
        count: i64,
    },
    ElapsedTime {
        event_code: u32,
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
    pub fn log_event(&mut self, metric_id: u32, event_code: u32) {
        let event_value = EventValue::Event { event_code };
        self.log_event_value(metric_id, event_value);
    }

    pub fn log_event_count(&mut self, metric_id: u32, event_code: u32, count: i64) {
        let event_value = EventValue::Count { event_code, count };
        self.log_event_value(metric_id, event_value);
    }

    pub fn log_elapsed_time(&mut self, metric_id: u32, event_code: u32, elapsed_micros: i64) {
        let event_value = EventValue::ElapsedTime {
            event_code,
            elapsed_micros,
        };
        self.log_event_value(metric_id, event_value);
    }

    pub fn log_int_histogram(&mut self, metric_id: u32, values: Vec<HistogramBucket>) {
        let event_value = EventValue::IntHistogram { values };
        self.log_event_value(metric_id, event_value);
    }

    fn log_event_value(&mut self, metric_id: u32, value: EventValue) {
        let event = Event { metric_id, value };
        if self.sender.try_send(event).is_err() {
            let was_blocked = self
                .is_blocked
                .compare_and_swap(false, true, Ordering::SeqCst);
            if !was_blocked {
                error!("cobalt sender drops a event/events: either buffer is full or no receiver is waiting");
            }
        } else {
            let was_blocked = self
                .is_blocked
                .compare_and_swap(true, false, Ordering::SeqCst);
            if was_blocked {
                info!("cobalt sender recovers and resumes sending")
            }
        }
    }
}

pub fn serve_with_project_name(buffer_size: usize, project_name: &str) -> (CobaltSender, impl Future<Output = ()>) {
    let (sender, receiver) = mpsc::channel(buffer_size);
    let sender = CobaltSender {
        sender,
        is_blocked: Arc::new(AtomicBool::new(false)),
    };
    let project_name = project_name.to_string();
    let fut = async move {
        let logger = match await!(get_cobalt_logger_with_project_name(project_name)) {
            Ok(logger) => logger,
            Err(e) => {
                error!("Error obtaining a Cobalt Logger: {}", e);
                return;
            }
        };
        await!(send_cobalt_events(logger, receiver))
    };
    (sender, fut)
}

pub fn serve(buffer_size: usize, config_path: &str) -> (CobaltSender, impl Future<Output = ()>) {
    let (sender, receiver) = mpsc::channel(buffer_size);
    let sender = CobaltSender {
        sender,
        is_blocked: Arc::new(AtomicBool::new(false)),
    };
    let config_path = config_path.to_string();
    let fut = async move {
        let logger = match await!(get_cobalt_logger(config_path)) {
            Ok(logger) => logger,
            Err(e) => {
                error!("Error obtaining a Cobalt Logger: {}", e);
                return;
            }
        };
        await!(send_cobalt_events(logger, receiver))
    };
    (sender, fut)
}

async fn get_cobalt_logger_with_project_name(project_name: String) -> Result<LoggerProxy, Error> {
    let (logger_proxy, server_end) =
        fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
    let logger_factory = fuchsia_app::client::connect_to_service::<LoggerFactoryMarker>()
        .context("Failed to connect to the Cobalt LoggerFactory")?;

    let res = await!(logger_factory.create_logger_from_project_name(
        &project_name,
        ReleaseStage::Ga,
        server_end,
    ));
    handle_cobalt_factory_result(res, "Failed to obtain Logger")?;
    Ok(logger_proxy)
}

async fn get_cobalt_logger(config_path: String) -> Result<LoggerProxy, Error> {
    let (logger_proxy, server_end) =
        fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
    let logger_factory = fuchsia_app::client::connect_to_service::<LoggerFactoryMarker>()
        .context("Failed to connect to the Cobalt LoggerFactory")?;

    let mut cobalt_config = File::open(config_path)?;
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
        Ok(other) => bail!("{}: {:?}", context, other),
        Err(e) => bail!("{}: {}", context, e),
    }
}

async fn send_cobalt_events(logger: LoggerProxy, mut receiver: mpsc::Receiver<Event>) {
    let mut is_full = false;
    while let Some(event) = await!(receiver.next()) {
        let resp = match event.value {
            EventValue::Event { event_code } => {
                await!(logger.log_event(
                    event.metric_id, event_code
                ))
            }
            EventValue::Count { event_code, count } => {
                await!(logger.log_event_count(
                    event.metric_id,
                    event_code,
                    "",
                    0, // TODO report a period duration once the backend supports it.
                    count
                ))
            }
            EventValue::ElapsedTime {
                event_code,
                elapsed_micros,
            } => {
                await!(logger.log_elapsed_time(
                    event.metric_id,
                    event_code,
                    "",
                    elapsed_micros
                ))
            }
            EventValue::IntHistogram { mut values } => {
                await!(logger.log_int_histogram(
                    event.metric_id,
                    0,
                    "",
                    &mut values.iter_mut()
                ))
            }
        };
        handle_cobalt_response(resp, event.metric_id, &mut is_full);
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
                bail!("Cobalt buffer became full. Cannot report the stats")
            }
            Ok(())
        }
        Ok(Status::Ok) => Ok(()),
        Ok(other) => bail!(
            "Cobalt returned an error for metric {}: {:?}",
            metric_id,
            other
        ),
        Err(e) => bail!(
            "Failed to send event to Cobalt for metric {}: {}",
            metric_id,
            e
        ),
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
