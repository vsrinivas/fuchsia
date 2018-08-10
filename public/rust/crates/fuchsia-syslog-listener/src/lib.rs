//! Rust fuchsia logger library.

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]
#![deny(missing_docs)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;

use app::client::connect_to_service;
use failure::{Error, ResultExt};
use fidl::encoding2::OutOfLine;
use futures::future::ready;

// Include the generated FIDL bindings for the `Logger` service.
extern crate fidl_fuchsia_logger;
use fidl_fuchsia_logger::{LogFilterOptions, LogListener, LogListenerImpl, LogListenerMarker,
                          LogListenerServer, LogMarker, LogMessage};

/// This trait is used to pass log message back to client.
pub trait LogProcessor {
    /// Called when log is recieved from logger.
    fn log(&mut self, message: LogMessage);

    /// Called when logger service signals that it is done dumping logs.
    /// This is only called if we request logger service to dump logs
    /// rather than registering a listener.
    fn done(&mut self);
}

fn log_listener<U>(processor: U) -> impl LogListener
where
    U: Sized + LogProcessor,
{
    LogListenerImpl {
        state: processor,
        on_open: |_, _| ready(()),
        done: |processor, _| {
            processor.done();
            // TODO(anmittal): close this server.
            ready(())
        },
        log: |processor, message, _| {
            processor.log(message);
            ready(())
        },
        log_many: |processor, messages, _| {
            for msg in messages {
                processor.log(msg);
            }
            ready(())
        },
    }
}

/// This fn will connect to fuchsia.logger.Log service and then
/// register listener or log dumper based on the parameters passed.
pub fn run_log_listener<U>(
    processor: U, options: Option<&mut LogFilterOptions>, dump_logs: bool,
) -> Result<LogListenerServer<impl LogListener>, Error>
where
    U: Sized + LogProcessor,
{
    let logger = connect_to_service::<LogMarker>()?;
    let (log_listener_local, log_listener_remote) = zx::Channel::create()?;
    let log_listener_local = async::Channel::from_channel(log_listener_local)?;
    let listener_ptr = fidl::endpoints2::ClientEnd::<LogListenerMarker>::new(log_listener_remote);

    let options = options.map(OutOfLine);
    if dump_logs {
        logger
            .dump_logs(listener_ptr, options)
            .context("failed to register log dumper")?;
    } else {
        logger
            .listen(listener_ptr, options)
            .context("failed to register listener")?;
    }

    Ok(log_listener(processor).serve(log_listener_local))
}
