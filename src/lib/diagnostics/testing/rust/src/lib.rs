// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use anyhow::Error;
use diagnostics_data::InspectData;
use diagnostics_reader::ArchiveReader;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogSinkMarker};
use fidl_fuchsia_sys::{EnvironmentControllerProxy, LauncherMarker};
use fuchsia_async::Task;
use fuchsia_component::{
    client::{connect_to_service, launch_with_options, App, ExitStatus, LaunchOptions},
    server::{ServiceFs, ServiceObj},
};
use fuchsia_syslog_listener::run_log_listener_with_proxy;
use futures::{channel::mpsc, StreamExt};
use std::ops::Deref;

/// An instance of [`fuchsia_component::client::App`] which will have all its logs and inspect
/// collected.
pub struct AppWithDiagnostics {
    app: App,
    observer: App,
    recv_logs: mpsc::UnboundedReceiver<LogMessage>,
    env_proxy: EnvironmentControllerProxy,
    _env_task: Task<()>,
    _listen_task: Task<()>,
}

const ARCHIVIST_URL: &str = "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx";

impl AppWithDiagnostics {
    /// Launch the app from the given URL with the given arguments, collecting its diagnostics.
    pub fn launch(url: impl ToString, args: Option<Vec<String>>) -> Self {
        Self::launch_with_options(url, args, LaunchOptions::new())
    }

    /// Launch the app from the given URL with the given arguments and launch options, collecting
    /// its diagnostics.
    pub fn launch_with_options(
        url: impl ToString,
        args: Option<Vec<String>>,
        launch_options: LaunchOptions,
    ) -> Self {
        let launcher = connect_to_service::<LauncherMarker>().unwrap();

        // we need an archivist to collect the diagnostics from the nested realm we make below
        let observer = launch_with_options(
            &launcher,
            ARCHIVIST_URL.to_owned(),
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
        let _listen_task = Task::spawn(async move {
            run_log_listener_with_proxy(&log_proxy, send_logs, Some(&mut options), false, None)
                .await
                .unwrap();
        });

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
        let _env_task = Task::spawn(Box::pin(async move {
            fs.collect::<()>().await;
        }));

        Self { app, observer, recv_logs, env_proxy, _env_task, _listen_task }
    }

    /// Read a snapshot of the app's inspect. Also returns inspect from any children of the app.
    ///
    /// This method should be called while the components of interest are still running, as
    /// Archivist does not yet preserve inspect from terminated components.
    pub async fn snapshot_inspect(&self) -> Result<Vec<InspectData>, Error> {
        let archive = self.observer.connect_to_service::<ArchiveAccessorMarker>()?;

        let mut results = ArchiveReader::new().with_archive(archive).get().await?;
        results.retain(|root| &root.metadata.component_url != ARCHIVIST_URL);

        Ok(results)
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

impl Deref for AppWithDiagnostics {
    type Target = App;
    fn deref(&self) -> &Self::Target {
        &self.app
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_inspect_tree;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn app_with_inspect() {
        let test_app = AppWithDiagnostics::launch(
            "fuchsia-pkg://fuchsia.com/diagnostics-testing-tests#meta/inspect_test_component.cmx",
            None,
        );

        let results = dbg!(test_app.snapshot_inspect().await.unwrap());
        assert_eq!(results.len(), 1, "expecting only one component's inspect");
        assert_inspect_tree!(results[0].payload.as_ref().unwrap(), root: {
            int: 3u64,
            "lazy-node": {
                a: "test",
                child: {
                    double: 3.14,
                },
            }
        });
    }
}
