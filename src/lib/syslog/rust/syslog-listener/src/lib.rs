// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Log listener support library.

#![deny(missing_docs)]

use anyhow::{Context as _, Error};
use fidl_fuchsia_diagnostics::LogInterestSelector;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogListenerSafeRequest, LogListenerSafeRequestStream, LogMarker, LogMessage,
    LogProxy,
};
use fuchsia_component::client::connect_to_protocol;
use futures::{channel::mpsc, TryStreamExt};

/// This trait is used to pass log message back to client.
pub trait LogProcessor {
    /// Called when log is received from logger.
    fn log(&mut self, message: LogMessage);

    /// Called when logger service signals that it is done dumping logs.
    /// This is only called if we request logger service to dump logs
    /// rather than registering a listener.
    fn done(&mut self);
}

async fn log_listener(
    mut processor: impl LogProcessor,
    mut stream: LogListenerSafeRequestStream,
) -> Result<(), fidl::Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            LogListenerSafeRequest::Log { log, responder } => {
                processor.log(log);
                responder.send().ok();
            }
            LogListenerSafeRequest::LogMany { log, responder } => {
                for msg in log {
                    processor.log(msg);
                }
                responder.send().ok();
            }
            LogListenerSafeRequest::Done { control_handle: _ } => {
                processor.done();
                return Ok(());
            }
        }
    }
    Ok(())
}

/// Register listener or log dumper based on the parameters passed.
pub async fn run_log_listener_with_proxy<'a>(
    logger: &LogProxy,
    processor: impl LogProcessor + 'a,
    options: Option<&'a mut LogFilterOptions>,
    dump_logs: bool,
    selectors: Option<&'a mut Vec<LogInterestSelector>>,
) -> Result<(), Error> {
    let (listener_ptr, listener_stream) = fidl::endpoints::create_request_stream()?;

    let options = options;
    if dump_logs {
        logger.dump_logs_safe(listener_ptr, options).context("failed to register log dumper")?;
    } else {
        match selectors {
            Some(s) => logger
                .listen_safe_with_selectors(listener_ptr, options, &mut s.into_iter())
                .context("failed to register listener with selectors")?,
            None => {
                logger.listen_safe(listener_ptr, options).context("failed to register listener")?
            }
        };
    }

    log_listener(processor, listener_stream).await?;
    Ok(())
}

/// This fn will connect to fuchsia.logger.Log service and then
/// register listener or log dumper based on the parameters passed.
pub async fn run_log_listener<'a>(
    processor: impl LogProcessor + 'a,
    options: Option<&'a mut LogFilterOptions>,
    dump_logs: bool,
    selectors: Option<&'a mut Vec<LogInterestSelector>>,
) -> Result<(), Error> {
    let logger = connect_to_protocol::<LogMarker>()?;
    run_log_listener_with_proxy(&logger, processor, options, dump_logs, selectors).await
}

impl LogProcessor for mpsc::UnboundedSender<LogMessage> {
    fn log(&mut self, message: LogMessage) {
        // this is called in spawned tasks which may outlive the test's interest
        self.unbounded_send(message).ok();
    }

    fn done(&mut self) {
        self.close_channel();
    }
}
