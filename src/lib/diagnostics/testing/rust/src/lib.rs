// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use diagnostics_data::{Data, InspectData};
use diagnostics_reader::{ArchiveReader, BatchIteratorType, ComponentSelector};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, ArchiveAccessorProxy};
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogSinkMarker};
use fidl_fuchsia_sys::{ComponentControllerEvent::*, LauncherProxy};
use fuchsia_async::Task;
use fuchsia_component::{
    client::{launch_with_options, App, LaunchOptions},
    server::ServiceFs,
};
use fuchsia_syslog_listener::run_log_listener_with_proxy;
use fuchsia_url::pkg_url::PkgUrl;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, prelude::*};

pub use diagnostics_data::{LifecycleType, Severity};
pub use diagnostics_reader::{Inspect, Lifecycle, Logs};
pub use fuchsia_inspect_node_hierarchy::assert_data_tree;

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cmx";

pub struct EnvWithDiagnostics {
    launcher: LauncherProxy,
    archivist: App,
    archive: ArchiveAccessorProxy,
    _env_task: Task<()>,
    listeners: Vec<Task<()>>,
}

impl EnvWithDiagnostics {
    /// Construct a new nested environment with a diagnostics archivist. Requires access to the
    /// `fuchsia.sys.Launcher` protocol.
    // TODO(fxbug.dev/58351) cooperate with run-test-component to avoid double-spawning archivist
    pub async fn new() -> Self {
        let mut fs = ServiceFs::new();
        let env = fs.create_salted_nested_environment("diagnostics").unwrap();
        let launcher = env.launcher().clone();
        let _env_task = Task::spawn(async move {
            let _env = env; // move env into the task so it stays alive
            fs.collect::<()>().await
        });

        // creating a proxy to logsink in our own environment, otherwise embedded archivist just
        // eats its own logs via logconnector
        let options = {
            let mut options = LaunchOptions::new();
            let (dir_client, dir_server) = zx::Channel::create().unwrap();
            let mut fs = ServiceFs::new();
            fs.add_proxy_service::<LogSinkMarker, _>().serve_connection(dir_server).unwrap();
            Task::spawn(fs.collect()).detach();
            options.set_additional_services(vec![LogSinkMarker::NAME.to_string()], dir_client);
            options
        };

        let archivist = launch_with_options(
            &launcher,
            ARCHIVIST_URL.to_string(),
            Some(vec!["--forward-logs".into()]),
            options,
        )
        .unwrap();
        let archive = archivist.connect_to_service::<ArchiveAccessorMarker>().unwrap();

        let mut archivist_events = archivist.controller().take_event_stream();
        if let OnTerminated { .. } = archivist_events.next().await.unwrap().unwrap() {
            panic!("archivist terminated early");
        }

        Self { archivist, archive, launcher, _env_task, listeners: vec![] }
    }

    /// Launch the app from the given URL with the given arguments, collecting its diagnostics.
    /// Returns a reader for the component's diagnostics.
    pub fn launch(&self, url: &str, args: Option<Vec<String>>) -> Launched {
        self.launch_with_options(url, args, LaunchOptions::new())
    }

    /// Launch the app from the given URL with the given arguments and launch options, collecting
    /// its diagnostics. Returns a reader for the component's diagnostics.
    pub fn launch_with_options(
        &self,
        url: &str,
        args: Option<Vec<String>>,
        launch_options: LaunchOptions,
    ) -> Launched {
        let url = PkgUrl::parse(url).unwrap();
        let manifest = url.resource().unwrap().rsplit('/').next().unwrap();
        let reader = self.reader_for(manifest, &[]);
        let app =
            launch_with_options(&self.launcher, url.to_string(), args, launch_options).unwrap();
        Launched { app, reader }
    }

    /// Returns the writer-half of a syslog socket, the reader half of which has been sent to
    /// the embedded archivist. The embedded archivist expects to receive logs in the legacy
    /// wire format. Pass this socket to [`fuchsia_syslog::init_with_socket_and_name`] to send
    /// the invoking component's logs to the embedded archivist.
    pub fn legacy_log_socket(&self) -> zx::Socket {
        let sink = self.archivist.connect_to_service::<LogSinkMarker>().unwrap();
        let (tx, rx) = zx::Socket::create(zx::SocketOpts::empty()).unwrap();
        sink.connect(rx).unwrap();
        tx
    }

    pub fn listen_to_logs(&mut self) -> impl Stream<Item = LogMessage> {
        // start listening
        let log_proxy = self.archivist.connect_to_service::<LogMarker>().unwrap();
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
        let listener = Task::spawn(async move {
            run_log_listener_with_proxy(&log_proxy, send_logs, Some(&mut options), false, None)
                .await
                .unwrap();
        });

        self.listeners.push(listener);
        recv_logs.filter(|m| {
            let from_archivist = m.tags.iter().any(|t| t == "archivist");
            async move { !from_archivist }
        })
    }

    /// Returns a reader for the provided manifest, assuming it was launched in this environment
    /// under the `realms` provided.
    pub fn reader_for(&self, manifest: &str, realms: &[&str]) -> AppReader {
        AppReader::new(self.archive.clone(), manifest, realms)
    }
}

pub struct Launched {
    pub app: App,
    pub reader: AppReader,
}

/// A reader for a launched component's inspect.
pub struct AppReader {
    reader: ArchiveReader,
}

impl AppReader {
    /// Construct a new `AppReader` with the given `archive` for the given `manifest`. Pass `realms`
    /// a list of nested environments relative to the archive if the component is not launched as
    /// a sibling to the archive.
    pub fn new(archive: ArchiveAccessorProxy, manifest: &str, realms: &[&str]) -> Self {
        let mut moniker = realms.iter().map(ToString::to_string).collect::<Vec<_>>();
        moniker.push(manifest.to_string());

        Self {
            reader: ArchiveReader::new()
                .with_archive(archive)
                .with_minimum_schema_count(1)
                .add_selector(ComponentSelector::new(moniker)),
        }
    }

    /// Returns a snapshot of the requested data for this component.
    pub async fn snapshot<T>(&self) -> Vec<Data<T::Key, T::Metadata>>
    where
        T: BatchIteratorType,
    {
        self.reader.snapshot::<T>().await.expect("snapshot will succeed")
    }

    /// Returns inspect data for this component.
    pub async fn inspect(&self) -> InspectData {
        self.snapshot::<Inspect>().await.into_iter().next().expect(">=1 item in results")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_diagnostics::Severity;
    use fuchsia_inspect::assert_inspect_tree;
    use futures::pin_mut;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn nested_apps_with_diagnostics() {
        let mut test_realm = EnvWithDiagnostics::new().await;
        let logs = test_realm.listen_to_logs();

        // launch the diagnostics emitter
        let Launched { app: _emitter, reader: emitter_reader } = test_realm.launch(
            "fuchsia-pkg://fuchsia.com/diagnostics-testing-tests#meta/emitter-for-test.cmx",
            None,
        );

        let emitter_inspect = emitter_reader.inspect().await;
        let nested_inspect =
            test_realm.reader_for("inspect_test_component.cmx", &[]).inspect().await;

        assert_inspect_tree!(emitter_inspect.payload.as_ref().unwrap(), root: {
            other_int: 7u64,
        });

        assert_inspect_tree!(nested_inspect.payload.as_ref().unwrap(), root: {
            int: 3u64,
            "lazy-node": {
                a: "test",
                child: {
                    double: 3.14,
                },
            }
        });

        async fn check_next_message(
            logs: &mut (impl Stream<Item = LogMessage> + Unpin),
            expected: &'static str,
        ) {
            let next_message = logs.next().await.unwrap();
            assert_eq!(next_message.tags, &["emitter_bin"]);
            assert_eq!(next_message.severity, Severity::Info as i32);
            assert_eq!(next_message.msg, expected);
        }

        pin_mut!(logs);
        check_next_message(&mut logs, "emitter started").await;
        check_next_message(&mut logs, "launching child").await;
    }
}
