// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust fuchsia logger library.

#![deny(warnings)]
#![deny(missing_docs)]

// TODO: Remove this line when #53984 is fixed in rust.
#![allow(deprecated)]

use fuchsia_app::client::connect_to_service;
use failure::{Error, ResultExt};
use fidl::encoding::OutOfLine;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::future::ready;


// Include the generated FIDL bindings for the `Logger` service.
use fidl_fuchsia_logger::{LogFilterOptions, LogListenerMarker,
                          LogListenerServer, LogMarker, LogMessage};
#[allow(deprecated)]
use fidl_fuchsia_logger::{LogListener, LogListenerImpl};

/// This trait is used to pass log message back to client.
pub trait LogProcessor {
    /// Called when log is recieved from logger.
    fn log(&mut self, message: LogMessage);

    /// Called when logger service signals that it is done dumping logs.
    /// This is only called if we request logger service to dump logs
    /// rather than registering a listener.
    fn done(&mut self);
}

#[allow(deprecated)]
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
#[allow(deprecated)]
pub fn run_log_listener<U>(
    processor: U, options: Option<&mut LogFilterOptions>, dump_logs: bool,
) -> Result<LogListenerServer<impl LogListener>, Error>
where
    U: Sized + LogProcessor,
{
    let logger = connect_to_service::<LogMarker>()?;
    let (log_listener_local, log_listener_remote) = zx::Channel::create()?;
    let log_listener_local = fasync::Channel::from_channel(log_listener_local)?;
    let listener_ptr = fidl::endpoints::ClientEnd::<LogListenerMarker>::new(log_listener_remote);

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
