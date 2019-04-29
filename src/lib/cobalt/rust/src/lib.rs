// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{bail, Error, ResultExt},
    fdio, fidl,
    fidl_fuchsia_cobalt::{
        CobaltEvent, CountEvent, EventPayload, HistogramBucket, LoggerFactoryMarker, LoggerProxy,
        ProjectProfile, ReleaseStage, Status,
    },
    fidl_fuchsia_mem as fuchsia_mem,
    fuchsia_component::client::connect_to_service,
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

pub trait AsEventCodes {
    fn as_event_codes(&self) -> Vec<u32>;
}

impl AsEventCodes for () {
    fn as_event_codes(&self) -> Vec<u32> {
        Vec::new()
    }
}

impl AsEventCodes for u32 {
    fn as_event_codes(&self) -> Vec<u32> {
        vec![*self]
    }
}

impl AsEventCodes for Vec<u32> {
    fn as_event_codes(&self) -> Vec<u32> {
        self.to_owned()
    }
}

impl AsEventCodes for [u32] {
    fn as_event_codes(&self) -> Vec<u32> {
        Vec::from(self)
    }
}

macro_rules! array_impls {
    ($($N:expr)+) => {
        $(
            impl AsEventCodes for [u32; $N] {
                fn as_event_codes(&self) -> Vec<u32> {
                    self[..].as_event_codes()
                }
            }
        )+
    }
}

array_impls! {0 1 2 3 4 5 6}

#[derive(Clone)]
pub struct CobaltSender {
    sender: mpsc::Sender<CobaltEvent>,
    is_blocked: Arc<AtomicBool>,
}

impl CobaltSender {
    pub fn new(sender: mpsc::Sender<CobaltEvent>) -> CobaltSender {
        CobaltSender { sender, is_blocked: Arc::new(AtomicBool::new(false)) }
    }

    pub fn log_event<Codes: AsEventCodes>(&mut self, metric_id: u32, event_codes: Codes) {
        self.log_event_value(CobaltEvent {
            metric_id,
            event_codes: event_codes.as_event_codes(),
            component: None,
            payload: EventPayload::Event(fidl_fuchsia_cobalt::Event {}),
        });
    }

    pub fn log_event_count<Codes: AsEventCodes>(
        &mut self,
        metric_id: u32,
        event_codes: Codes,
        count: i64,
    ) {
        self.log_event_value(CobaltEvent {
            metric_id,
            event_codes: event_codes.as_event_codes(),
            component: None,
            payload: EventPayload::EventCount(CountEvent { period_duration_micros: 0, count }),
        });
    }

    pub fn log_elapsed_time<Codes: AsEventCodes>(
        &mut self,
        metric_id: u32,
        event_codes: Codes,
        elapsed_micros: i64,
    ) {
        self.log_event_value(CobaltEvent {
            metric_id,
            event_codes: event_codes.as_event_codes(),
            component: None,
            payload: EventPayload::ElapsedMicros(elapsed_micros),
        });
    }

    pub fn log_int_histogram(&mut self, metric_id: u32, values: Vec<HistogramBucket>) {
        self.log_event_value(CobaltEvent {
            metric_id,
            event_codes: vec![0],
            component: None,
            payload: EventPayload::IntHistogram(values),
        });
    }

    pub fn log_cobalt_event(&mut self, event: CobaltEvent) {
        self.log_event_value(event);
    }

    fn log_event_value(&mut self, event: CobaltEvent) {
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

pub fn serve_with_project_name(
    buffer_size: usize,
    project_name: &str,
) -> (CobaltSender, impl Future<Output = ()>) {
    let (sender, receiver) = mpsc::channel(buffer_size);
    let sender = CobaltSender::new(sender);
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
    let sender = CobaltSender::new(sender);
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
    let logger_factory = connect_to_service::<LoggerFactoryMarker>()
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
    let logger_factory = connect_to_service::<LoggerFactoryMarker>()
        .context("Failed to connect to the Cobalt LoggerFactory")?;

    let mut cobalt_config = File::open(config_path)?;
    let vmo = fdio::get_vmo_copy_from_file(&cobalt_config)?;
    let size = cobalt_config.seek(std::io::SeekFrom::End(0))?;

    let config = fuchsia_mem::Buffer { vmo, size };

    let res = await!(logger_factory.create_logger(
        &mut ProjectProfile { config, release_stage: ReleaseStage::Ga },
        server_end,
    ));
    handle_cobalt_factory_result(res, "Failed to obtain Logger")?;
    Ok(logger_proxy)
}

fn handle_cobalt_factory_result(
    r: Result<Status, fidl::Error>,
    context: &str,
) -> Result<(), failure::Error> {
    match r {
        Ok(Status::Ok) => Ok(()),
        Ok(other) => bail!("{}: {:?}", context, other),
        Err(e) => bail!("{}: {}", context, e),
    }
}

async fn send_cobalt_events(logger: LoggerProxy, mut receiver: mpsc::Receiver<CobaltEvent>) {
    let mut is_full = false;
    while let Some(mut event) = await!(receiver.next()) {
        let resp = if event.event_codes.len() == 1 {
            match event.payload {
                EventPayload::Event(_) => {
                    await!(logger.log_event(event.metric_id, event.event_codes[0]))
                }
                EventPayload::EventCount(CountEvent { period_duration_micros, count }) => {
                    await!(logger.log_event_count(
                        event.metric_id,
                        event.event_codes[0],
                        &event.component.unwrap_or_else(String::new),
                        period_duration_micros,
                        count
                    ))
                }
                EventPayload::ElapsedMicros(elapsed_micros) => await!(logger.log_elapsed_time(
                    event.metric_id,
                    event.event_codes[0],
                    &event.component.unwrap_or_else(String::new),
                    elapsed_micros
                )),
                EventPayload::IntHistogram(mut values) => await!(logger.log_int_histogram(
                    event.metric_id,
                    event.event_codes[0],
                    &event.component.unwrap_or_else(String::new),
                    &mut values.iter_mut()
                )),
                _ => await!(logger.log_cobalt_event(&mut event)),
            }
        } else {
            await!(logger.log_cobalt_event(&mut event))
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
    resp: Result<Status, fidl::Error>,
    metric_id: u32,
    is_full: &mut bool,
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
        Ok(other) => bail!("Cobalt returned an error for metric {}: {:?}", metric_id, other),
        Err(e) => bail!("Failed to send event to Cobalt for metric {}: {}", metric_id, e),
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

        let cobalt_resp = Err(fidl::Error::ClientWrite(fuchsia_zircon::Status::PEER_CLOSED));
        assert!(throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, false);
    }

    #[test]
    fn test_as_event_codes() {
        assert_eq!(().as_event_codes(), vec![]);
        assert_eq!([].as_event_codes(), vec![]);
        assert_eq!(1.as_event_codes(), vec![1]);
        assert_eq!([1].as_event_codes(), vec![1]);
        assert_eq!(vec![1].as_event_codes(), vec![1]);
        assert_eq!([1, 2].as_event_codes(), vec![1, 2]);
        assert_eq!(vec![1, 2].as_event_codes(), vec![1, 2]);
        assert_eq!([1, 2, 3].as_event_codes(), vec![1, 2, 3]);
        assert_eq!(vec![1, 2, 3].as_event_codes(), vec![1, 2, 3]);
        assert_eq!([1, 2, 3, 4].as_event_codes(), vec![1, 2, 3, 4]);
        assert_eq!(vec![1, 2, 3, 4].as_event_codes(), vec![1, 2, 3, 4]);
        assert_eq!([1, 2, 3, 4, 5].as_event_codes(), vec![1, 2, 3, 4, 5]);
        assert_eq!(vec![1, 2, 3, 4, 5].as_event_codes(), vec![1, 2, 3, 4, 5]);
    }

    #[test]
    fn test_cobalt_sender() {
        let (sender, mut receiver) = mpsc::channel(1);
        let mut sender = CobaltSender::new(sender);
        sender.log_event(1, 1);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 1,
                event_codes: vec![1],
                component: None,
                payload: EventPayload::Event(fidl_fuchsia_cobalt::Event {}),
            }
        );

        sender.log_event_count(2, (), 1);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 2,
                event_codes: vec![],
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 1
                })
            }
        );

        sender.log_elapsed_time(3, [1, 2], 30);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 3,
                event_codes: vec![1, 2],
                component: None,
                payload: EventPayload::ElapsedMicros(30),
            }
        );

        sender.log_int_histogram(4, vec![HistogramBucket { index: 2, count: 2 }]);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 4,
                event_codes: vec![0],
                component: None,
                payload: EventPayload::IntHistogram(vec![HistogramBucket { index: 2, count: 2 }]),
            }
        );
    }
}
