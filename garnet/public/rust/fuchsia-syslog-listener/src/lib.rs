// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust fuchsia logger library.

#![deny(missing_docs)]

use anyhow::{Context as _, Error};

use fuchsia_component::client::connect_to_service;
use futures::TryStreamExt;
// Include the generated FIDL bindings for the `Logger` service.
use fidl_fuchsia_logger::{
    LogFilterOptions, LogListenerRequest, LogListenerRequestStream, LogMarker, LogMessage,
};

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
    mut stream: LogListenerRequestStream,
) -> Result<(), fidl::Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            LogListenerRequest::Log { log, control_handle: _ } => {
                processor.log(log);
            }
            LogListenerRequest::LogMany { log, control_handle: _ } => {
                for msg in log {
                    processor.log(msg);
                }
            }
            LogListenerRequest::Done { control_handle: _ } => {
                processor.done();
                return Ok(());
            }
        }
    }
    Ok(())
}

/// This fn will connect to fuchsia.logger.Log service and then
/// register listener or log dumper based on the parameters passed.
pub async fn run_log_listener<'a>(
    processor: impl LogProcessor + 'a,
    options: Option<&'a mut LogFilterOptions>,
    dump_logs: bool,
) -> Result<(), Error> {
    let logger = connect_to_service::<LogMarker>()?;
    let (listener_ptr, listener_stream) = fidl::endpoints::create_request_stream()?;

    let options = options;
    if dump_logs {
        logger.dump_logs(listener_ptr, options).context("failed to register log dumper")?;
    } else {
        logger.listen(listener_ptr, options).context("failed to register listener")?;
    }

    log_listener(processor, listener_stream).await?;
    Ok(())
}
