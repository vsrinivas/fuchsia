// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use fidl_fuchsia_tracing_controller::{
    ControllerMarker, ControllerProxy, StartErrorCode, StartOptions, StopOptions, TerminateOptions,
    TraceConfig,
};
use fuchsia_async;
use fuchsia_component::{self as app};
use fuchsia_zircon as zx;
use futures::{future, io::AsyncReadExt, TryFutureExt};
use parking_lot::RwLock;
use std::fs;
use std::io::Write;

const RAW_TRACE_FILE: &'static str = "/custom_artifacts/trace.fxt";
const JSON_TRACE_FILE: &'static str = "/custom_artifacts/trace.json";
const BUFFER_SIZE_MB: u32 = 36;

struct Status {
    controller: Option<ControllerProxy>,
    data_socket: Option<zx::Socket>,
}

pub struct SingleSessionTrace {
    status: RwLock<Status>,
}

/// Provides controls to capture a single trace session
///
/// To use:
/// ```
/// let trace = SingleSessionTrace::new();
/// trace.initialize(args.trace_config).await?;
/// trace.start().await?;
/// ...
/// trace.stop().await?;
/// trace.terminate().await?
/// ```
impl SingleSessionTrace {
    pub fn new() -> SingleSessionTrace {
        SingleSessionTrace { status: RwLock::new(Status { controller: None, data_socket: None }) }
    }

    /// Initialize a trace session.
    ///
    /// A trace session allows for starting and stopping the collection of trace data. Trace data
    /// is then returned all at once when [terminate] is called.
    ///
    /// For documentation on the args parameter, see
    /// [InitializeRequest](crate::tracing::types::InitializeRequest).
    ///
    /// There can only be one trace session active on the system at a time. If there is a trace
    /// session from another controller active on the system, initialize may still return
    /// success, as trace_manager accepts the initialize_tracing call as a no-op. If needed,
    /// [terminate] may be used to ensure that no trace session is active on the system.
    pub async fn initialize(&self, categories: Vec<String>) -> Result<(), Error> {
        let trace_controller = app::client::connect_to_protocol::<ControllerMarker>()?;
        let (write_socket, read_socket) = zx::Socket::create(zx::SocketOpts::STREAM)?;
        let config = TraceConfig {
            buffer_size_megabytes_hint: Some(BUFFER_SIZE_MB),
            categories: Some(categories),
            ..TraceConfig::EMPTY
        };

        trace_controller.initialize_tracing(config, write_socket)?;
        {
            let mut status = self.status.write();
            status.data_socket = Some(read_socket);
            status.controller = Some(trace_controller);
        }
        Ok(())
    }

    /// Start tracing.
    ///
    /// There must be a trace session initialized first, otherwise an error is returned.
    /// Within a trace session, tracing may be started and stopped multiple times.
    pub async fn start(&self) -> Result<(), Error> {
        let status = self.status.read();
        let trace_controller = status
            .controller
            .as_ref()
            .ok_or_else(|| format_err!("No trace session has been initialized"))?;
        match trace_controller.start_tracing(StartOptions::EMPTY).await? {
            Ok(_) => Ok(()),
            Err(e) => match e {
                StartErrorCode::NotInitialized => {
                    Err(format_err!("trace_manager reports trace not initialized"))
                }
                StartErrorCode::AlreadyStarted => Err(format_err!("Trace already started")),
                StartErrorCode::Stopping => Err(format_err!("Trace is stopping")),
                StartErrorCode::Terminating => Err(format_err!("Trace is terminating")),
            },
        }
    }

    /// Stop tracing.
    ///
    /// There must be a trace session initialized first, otherwise an error is returned.
    /// Within a trace session, tracing may be started and stopped multiple times.
    pub async fn stop(&self) -> Result<(), Error> {
        let status = self.status.read();
        let trace_controller = status
            .controller
            .as_ref()
            .ok_or_else(|| format_err!("No trace session has been initialized"))?;
        trace_controller.stop_tracing(StopOptions::EMPTY).await?;
        Ok(())
    }

    /// Terminate tracing and convert data to json using `trace2json`.
    ///
    /// Both raw trace file and converted trace json file will be stored in test's custom artifact.
    pub async fn terminate(&self) -> Result<(), Error> {
        let controller = match self.status.write().controller.take() {
            Some(controller) => controller,
            None => app::client::connect_to_protocol::<ControllerMarker>()?,
        };
        let options = TerminateOptions { write_results: Some(true), ..TerminateOptions::EMPTY };
        let terminate_fut = controller.terminate_tracing(options).map_err(Error::from);
        let data_socket = self.status.write().data_socket.take();
        let drain_fut = drain_socket(data_socket);
        // Note: It is important that these two futures are handled concurrently, as trace_manager
        // writes the trace data before completing the terminate FIDL call.
        let (_terminate_result, drain_result) = future::try_join(terminate_fut, drain_fut).await?;

        // write this to file
        let mut raw_trace_file = fs::File::create(RAW_TRACE_FILE)?;
        raw_trace_file.write_all(&drain_result)?;
        let output = std::process::Command::new("/pkg/bin/trace2json")
            .args([
                format!("--input-file={}", RAW_TRACE_FILE),
                format!("--output-file={}", JSON_TRACE_FILE),
            ])
            .output()
            .context("waiting for trace2json to finish")?;
        if !output.status.success() {
            return Err(format_err!("trace2json failed: output.stderr: {:?}", output.stderr));
        }

        Ok(())
    }
}

async fn drain_socket(socket: Option<zx::Socket>) -> Result<Vec<u8>, Error> {
    let mut ret = Vec::new();
    if let Some(socket) = socket {
        let mut socket = fuchsia_async::Socket::from_socket(socket)?;
        socket.read_to_end(&mut ret).await?;
    }
    Ok(ret)
}
