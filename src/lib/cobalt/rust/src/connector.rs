// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Contains the logic for connecting to the Cobalt FIDL service, as well as setting up a worker
//! thread to handle sending `CobaltEvent`s off of the main thread.

use {
    crate::sender::CobaltSender,
    anyhow::{format_err, Context as _, Error},
    fidl,
    fidl_fuchsia_cobalt::{CobaltEvent, LoggerFactoryMarker, LoggerProxy, Status},
    fuchsia_component::client::connect_to_service,
    futures::{channel::mpsc, prelude::*},
    log::error,
};

/// Determines how to connect to the Cobalt FIDL service.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum ConnectionType {
    /// Connecting with ProjectId relies on the Cobalt FIDL service's internal copy of the metrics
    /// registry.
    ProjectId(u32),
}

impl ConnectionType {
    /// Constructs a `ConnectionType::ProjectId(_)`
    pub fn project_id(project_id: u32) -> Self {
        ConnectionType::ProjectId(project_id)
    }
}

/// Information required for connecting to the Cobalt FIDL service.
#[derive(Debug, Clone, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct CobaltConnector {
    /// The size of the `mpsc::channel` to use when sending `CobaltEvent`s from the main thread to
    /// the worker thread.
    pub buffer_size: usize,
}

/// At the time of this writing, all clients are using a buffer size of 100, so we've set this as
/// the default value. It is unclear whether this is a good default since it was originally
/// chosen arbitrarily, so for now it may be considered a placeholder value.
///
/// This number determines how backed up the cobalt sender thread can get before the
/// `CobaltSender` is unable to queue up more `CobaltEvent`s.
pub const DEFAULT_BUFFER_SIZE: usize = 100;

impl Default for CobaltConnector {
    fn default() -> Self {
        Self { buffer_size: DEFAULT_BUFFER_SIZE }
    }
}

impl CobaltConnector {
    /// Connects to the Cobalt FIDL service. And returns a tuple of type `(CobaltSender, impl
    /// Future<Output = ()>)`. The `CobaltSender` is used for sending cobalt events to the FIDL
    /// service, and the `impl Future<Output = ()>` represents the sending thread and can be joined
    /// against.
    ///
    /// # Arguments
    ///
    /// * `connection_type` - The `ConnectionType` desired.
    pub fn serve(
        self,
        connection_type: ConnectionType,
    ) -> (CobaltSender, impl Future<Output = ()>) {
        let (sender, receiver) = mpsc::channel(self.buffer_size);
        let sender = CobaltSender::new(sender);
        let fut = async move {
            let logger = match self.get_cobalt_logger(connection_type).await {
                Ok(logger) => logger,
                Err(e) => {
                    error!("Error obtaining a Cobalt Logger: {}", e);
                    return;
                }
            };
            Self::send_cobalt_events(logger, receiver).await
        };
        (sender, fut)
    }

    async fn get_cobalt_logger(
        self,
        connection_type: ConnectionType,
    ) -> Result<LoggerProxy, Error> {
        let (logger_proxy, server_end) =
            fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
        let logger_factory = connect_to_service::<LoggerFactoryMarker>()
            .context("Failed to connect to the Cobalt LoggerFactory")?;

        let res = match connection_type {
            ConnectionType::ProjectId(project_id) => {
                logger_factory.create_logger_from_project_id(project_id, server_end).await
            }
        };

        Self::handle_cobalt_factory_result(res, "failed to obtain logger")?;
        Ok(logger_proxy)
    }

    fn handle_cobalt_factory_result(
        r: Result<Status, fidl::Error>,
        context: &str,
    ) -> Result<(), anyhow::Error> {
        match r {
            Ok(Status::Ok) => Ok(()),
            Ok(other) => return Err(format_err!("{}: {:?}", context, other)),
            Err(e) => return Err(format_err!("{}: {}", context, e)),
        }
    }

    async fn send_cobalt_events(logger: LoggerProxy, mut receiver: mpsc::Receiver<CobaltEvent>) {
        let mut log_error = log_first_n_factory(30, |e| error!("{}", e));
        while let Some(mut event) = receiver.next().await {
            let resp = logger.log_cobalt_event(&mut event).await;
            match resp {
                Ok(Status::Ok) => continue,
                Ok(other) => log_error(format!(
                    "Cobalt returned an error for metric {}: {:?}",
                    event.metric_id, other
                )),
                Err(e) => log_error(format!(
                    "Failed to send event to Cobalt for metric {}: {}",
                    event.metric_id, e
                )),
            }
        }
    }
}

/// Takes a value `n` which represents the number of times to log messages and a function, `log_fn`
/// that is called to perform the logging and returns a function that will only log the first `n`
/// messages.
fn log_first_n_factory(n: u64, mut log_fn: impl FnMut(String)) -> impl FnMut(String) {
    let mut count = 0;
    move |message| {
        if count < n {
            count += 1;
            log_fn(message);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn log_first_0() {
        let mut messages_logged_count = 0;
        {
            let log_fn = |_| messages_logged_count += 1;
            let mut log = log_first_n_factory(0, log_fn);
            log("test message".into());
        }
        assert_eq!(messages_logged_count, 0);
    }

    #[test]
    fn log_first_1() {
        let mut messages_logged_count = 0;
        {
            let log_fn = |_| messages_logged_count += 1;
            let mut log = log_first_n_factory(1, log_fn);
            log("test message 1".into());
            log("test message 2".into());
        }
        assert_eq!(messages_logged_count, 1);
    }

    #[test]
    fn log_first_2() {
        let mut messages_logged_count = 0;
        {
            let log_fn = |_| messages_logged_count += 1;
            let mut log = log_first_n_factory(2, log_fn);
            log("test message 1".into());
            log("test message 2".into());
            log("test message 3".into());
            log("test message 4".into());
        }
        assert_eq!(messages_logged_count, 2);
    }
}
