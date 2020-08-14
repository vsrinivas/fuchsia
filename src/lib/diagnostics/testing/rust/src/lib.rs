// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use anyhow::Error;
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

pub use diagnostics_data::LifecycleType;
pub use diagnostics_reader::{Inspect, Lifecycle};

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
    /// Launch the app from the given URL with the given arguments in the named realm, collecting
    /// its diagnostics.
    pub fn launch(realm: impl ToString, url: impl ToString, args: Option<Vec<String>>) -> Self {
        Self::launch_with_options(realm, url, args, LaunchOptions::new())
    }

    /// Launch the app from the given URL with the given arguments and launch options in the
    /// named realm, collecting its diagnostics.
    pub fn launch_with_options(
        realm: impl ToString,
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
        let realm_label = realm.to_string();
        let dir_req = observer.directory_request().clone();
        let mut fs = ServiceFs::<ServiceObj<'_, ()>>::new();
        let (env_proxy, app) = fs
            .add_proxy_service_to::<LogSinkMarker, _>(dir_req)
            .launch_component_in_nested_environment_with_options(
                url.to_string(),
                args,
                launch_options,
                &realm_label,
            )
            .unwrap();
        let _env_task = Task::spawn(Box::pin(async move {
            fs.collect::<()>().await;
        }));

        Self { app, observer, recv_logs, env_proxy, _env_task, _listen_task }
    }

    pub fn reader(&self) -> ArchiveReader {
        let archive = self.observer.connect_to_service::<ArchiveAccessorMarker>().unwrap();
        ArchiveReader::new().with_archive(archive).without_url(ARCHIVIST_URL)
    }

    /// Returns once the component with `moniker` has started.
    pub async fn until_has_started(&self, moniker: &str) -> Result<(), Error> {
        self.until_all_have_started(&[moniker]).await
    }

    /// Returns once all the components in `monikers` have started.
    pub async fn until_all_have_started(&self, monikers: &[&str]) -> Result<(), Error> {
        loop {
            let lifecycle = self.reader().snapshot::<Lifecycle>().await?;
            if monikers.iter().all(|moniker| {
                lifecycle
                    .iter()
                    .filter(|e| e.metadata.lifecycle_event_type == LifecycleType::Started)
                    .any(|e| e.moniker.ends_with(moniker))
            }) {
                return Ok(());
            }
        }
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
    use fidl_fuchsia_diagnostics::Severity;
    use fuchsia_inspect::assert_inspect_tree;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn nested_apps_with_diagnostics() {
        let emitter = "emitter-for-test.cmx";
        let emitter_url =
            format!("fuchsia-pkg://fuchsia.com/diagnostics-testing-tests#meta/{}", emitter);

        let test_realm = "with-diagnostics";
        let emitter_moniker = format!("{}/{}", test_realm, emitter);
        let nested_moniker = format!("{}/inspect_test_component.cmx", test_realm);

        // launch the diagnostics emitter
        let test_app = AppWithDiagnostics::launch(test_realm, emitter_url, None);

        // wait for start of both parent and child
        test_app.until_all_have_started(&[&emitter_moniker, &emitter_moniker]).await.unwrap();

        // snapshot the environment's inspect
        let reader = test_app.reader().with_minimum_schema_count(2);
        let mut results = reader.snapshot::<Inspect>().await.unwrap();
        // sorting by moniker puts the parent first
        results.sort_by(|a, b| a.moniker.cmp(&b.moniker));

        assert_eq!(results.len(), 2, "expecting inspect for both components in the env");
        let (emitter_inspect, nested_inspect) = (results[0].clone(), results[1].clone());

        assert_eq!(&emitter_inspect.moniker, &emitter_moniker);
        assert_inspect_tree!(emitter_inspect.payload.as_ref().unwrap(), root: {
            other_int: 7u64,
        });

        assert_eq!(&nested_inspect.moniker, &nested_moniker);
        assert_inspect_tree!(nested_inspect.payload.as_ref().unwrap(), root: {
            int: 3u64,
            "lazy-node": {
                a: "test",
                child: {
                    double: 3.14,
                },
            }
        });

        // end the child task and wait for it to finish
        let (_status, mut logs) = test_app.kill().await;

        logs.sort_by_key(|l| l.time);
        let mut logs_iter = logs.iter();
        let mut check_next_message = |expected| {
            let next_message = logs_iter.next().unwrap();

            assert_eq!(next_message.tags, &["emitter_bin"]);
            assert_eq!(next_message.severity, Severity::Info as i32);
            assert_eq!(next_message.msg, expected);
        };

        check_next_message("emitter started");
        check_next_message("launching child");
        assert_eq!(logs_iter.next(), None);
    }
}
