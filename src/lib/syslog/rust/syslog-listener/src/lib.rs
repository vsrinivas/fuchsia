// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Log listener support library.

#![deny(missing_docs)]

use anyhow::{Context as _, Error};

use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogInterestSelector, LogLevelFilter, LogListenerSafeRequest,
    LogListenerSafeRequestStream, LogMarker, LogMessage, LogProxy, LogSinkMarker,
};
use fidl_fuchsia_sys::{EnvironmentControllerProxy, LauncherMarker};
use fuchsia_async as fasync;
use fuchsia_component::{
    client::{connect_to_service, launch_with_options, App, ExitStatus, LaunchOptions},
    server::{ServiceFs, ServiceObj},
};
use futures::{channel::mpsc, StreamExt, TryStreamExt};
use std::ops::Deref;

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
    let logger = connect_to_service::<LogMarker>()?;
    run_log_listener_with_proxy(&logger, processor, options, dump_logs, selectors).await
}

/// An instance of [`fuchsia_component::client::App`] which will have all its `LogSink` messages
/// collected. Not recommended for production use since the intended use case of ergonomic tests
/// biases the implementation towards convenience and e.g. spawns multiple tasks when launching the
/// app.
pub struct AppWithLogs {
    app: App,
    observer: App,
    recv_logs: mpsc::UnboundedReceiver<LogMessage>,
    env_proxy: EnvironmentControllerProxy,
}

impl AppWithLogs {
    /// Launch the app from the given URL with the given arguments, collecting its logs.
    pub fn launch(url: impl ToString, args: Option<Vec<String>>) -> Self {
        Self::launch_with_options(url, args, LaunchOptions::new())
    }

    /// Launch the app from the given URL with the given arguments and launch options, collecting
    /// its logs.
    pub fn launch_with_options(
        url: impl ToString,
        args: Option<Vec<String>>,
        launch_options: LaunchOptions,
    ) -> Self {
        let launcher = connect_to_service::<LauncherMarker>().unwrap();

        // we need an observer.cmx to collect the logs from the nested realm we make below
        let observer = launch_with_options(
            &launcher,
            "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx".to_owned(),
            // the log connector api is challenging to integrate with the stop api
            Some(vec!["--disable-log-connector".to_owned()]),
            LaunchOptions::new(),
        )
        .unwrap();

        // start listening
        let log_proxy = observer.connect_to_service::<LogMarker>().unwrap();
        let mut options = LogFilterOptions {
            filter_by_pid: false,
            pid: 0,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            filter_by_tid: false,
            tid: 0,
            tags: vec![],
        };
        let (send_logs, recv_logs) = mpsc::unbounded();
        fasync::Task::spawn(async move {
            run_log_listener_with_proxy(&log_proxy, send_logs, Some(&mut options), false, None)
                .await
                .unwrap();
        })
        .detach();

        // start the component
        let dir_req = observer.directory_request().clone();
        let mut fs = ServiceFs::<ServiceObj<'_, ()>>::new();
        let (env_proxy, app) = fs
            .add_proxy_service_to::<LogSinkMarker, _>(dir_req)
            .launch_component_in_nested_environment_with_options(
                url.to_string(),
                args,
                launch_options,
                "logged",
            )
            .unwrap();
        fasync::Task::spawn(Box::pin(async move {
            fs.collect::<()>().await;
        }))
        .detach();

        Self { app, observer, recv_logs, env_proxy }
    }

    /// Kill the running component. Differs from [`fuchsia_component::client::App`] in that it also
    /// returns a future which waits for the component to exit.
    pub async fn kill(mut self) -> (ExitStatus, Vec<LogMessage>) {
        self.app.kill().unwrap();
        self.wait().await
    }

    /// Wait until the running component has terminated, returning its exit status and logs.
    pub async fn wait(mut self) -> (ExitStatus, Vec<LogMessage>) {
        // wait for logging_component to die
        let status = self.app.wait().await.unwrap();

        // kill environment before stopping observer.
        self.env_proxy.kill().await.unwrap();

        // connect to controller and call stop
        let controller = self.observer.connect_to_service::<ControllerMarker>().unwrap();
        controller.stop().unwrap();

        // collect all logs
        let mut logs = self
            .recv_logs
            .collect::<Vec<_>>()
            .await
            .into_iter()
            .filter(|m| !m.tags.iter().any(|t| t == "observer" || t == "archivist"))
            .collect::<Vec<_>>();

        // recv_logs returned, means observer must be dead. check.
        assert!(self.observer.wait().await.unwrap().success());

        // in case we got things out of order
        logs.sort_by_key(|msg| msg.time);

        (status, logs)
    }
}

impl Deref for AppWithLogs {
    type Target = App;
    fn deref(&self) -> &Self::Target {
        &self.app
    }
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
