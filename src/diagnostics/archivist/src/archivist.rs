// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{archive, archive_accessor, configs, data_stats, diagnostics, events, inspect, logs},
    anyhow::{format_err, Error},
    fidl_fuchsia_diagnostics_test::{ControllerRequest, ControllerRequestStream},
    fidl_fuchsia_sys_internal::{ComponentEventProviderProxy, LogConnectorProxy, SourceIdentity},
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        future::{self, abortable, Either, FutureObj},
        prelude::*,
        stream,
    },
    io_util,
    parking_lot::RwLock,
    std::{path::Path, sync::Arc},
};

/// Spawns controller sends stop signal.
fn spawn_controller(mut stream: ControllerRequestStream, mut stop_sender: mpsc::Sender<()>) {
    fasync::spawn(
        async move {
            while let Some(ControllerRequest::Stop { .. }) = stream.try_next().await? {
                stop_sender.send(()).await.ok();
                break;
            }
            Ok(())
        }
        .map(|o: Result<(), fidl::Error>| {
            if let Err(e) = o {
                eprintln!("error serving controller: {}", e);
            }
            ()
        }),
    );
}

/// The `Archivist` is responsible for publishing all the services and monitoring component's health.
/// # All resposibilities:
///  * Run and process Log Sink connections on main future.
///  * Run and Process Log Listener connections by spawning them.
///  * Optionally collect component events.
pub struct Archivist {
    /// Instance of log manager which services all the logs.
    log_manager: logs::LogManager,

    /// Archive state.
    state: archive::ArchivistState,

    /// True if pipeline exists.
    pipeline_exists: bool,

    /// Store for safe keeping,
    _pipeline_nodes: Vec<fuchsia_inspect::Node>,

    // Store for safe keeping.
    _pipeline_configs: Vec<configs::PipelineConfig>,

    /// ServiceFs object to server outgoing directory.
    fs: ServiceFs<ServiceObj<'static, ()>>,

    /// Stream which will recieve all the futures to process LogSink connections.
    log_sinks: Option<mpsc::UnboundedReceiver<FutureObj<'static, ()>>>,

    /// Provider to optionally collect component events.
    provider: Option<ComponentEventProviderProxy>,

    /// Recieve stop signal to kill this archivist.
    stop_recv: Option<mpsc::Receiver<()>>,
}

impl Archivist {
    async fn collect_component_events(
        provider: ComponentEventProviderProxy,
        state: archive::ArchivistState,
        pipeline_exists: bool,
    ) -> Result<(), Error> {
        let events_result =
            events::legacy::listen(provider, diagnostics::root().create_child("event_stats")).await;

        let events = match events_result {
            Ok(events) => {
                if !pipeline_exists {
                    component::health().set_unhealthy("Pipeline config has an error");
                } else {
                    component::health().set_ok();
                }
                events
            }
            Err(e) => {
                component::health().set_unhealthy(&format!(
                    "Failed to listen for component lifecycle events: {:?}",
                    e
                ));
                stream::empty().boxed()
            }
        };
        archive::run_archivist(state, events).await
    }

    // Sets log connector which is used to server attributed LogSink.
    pub fn set_log_connector(&mut self, log_connector: LogConnectorProxy) -> &mut Self {
        self.log_manager.spawn_log_consumer(log_connector);
        return self;
    }

    /// Install controller service.
    pub fn install_controller_service(&mut self) -> &mut Self {
        let (stop_sender, stop_recv) = mpsc::channel(0);
        self.fs
            .dir("svc")
            .add_fidl_service(move |stream| spawn_controller(stream, stop_sender.clone()));
        self.stop_recv = Some(stop_recv);
        return self;
    }

    /// Installs `LogSink` and `Log` services. Panics if called twice.
    /// # Arguments:
    /// * `log_connector` - If provided, install log connector.
    pub fn install_logger_services(&mut self) -> &mut Self {
        assert!(self.log_sinks.is_none(), "Cannot install services twice.");

        let log_manager = self.log_manager().clone();
        let log_manager_clone = self.log_manager().clone();
        let (sink_sender, sinks) = mpsc::unbounded();

        self.fs
            .dir("svc")
            .add_fidl_service(move |stream| log_manager_clone.spawn_log_handler(stream))
            .add_fidl_service(move |stream| {
                let fut = log_manager.clone().process_log_sink(stream, SourceIdentity::empty());
                if let Err(e) = sink_sender.unbounded_send(FutureObj::new(Box::new(fut))) {
                    eprintln!("Can't queue log sink connection, {}", e);
                }
            });
        self.log_sinks = Some(sinks);
        return self;
    }

    // Sets event provider which is used to collect component events, Panics if called twice.
    pub fn set_event_provider(&mut self, provider: ComponentEventProviderProxy) -> &mut Self {
        assert!(self.provider.is_none(), "set_event_provider called twice.");
        self.provider = Some(provider);
        return self;
    }

    /// Creates new instance, sets up inspect and adds 'archive' directory to output folder.
    /// Also installs `fuchsia.diagnostics.Archive` service.
    /// Call `install_logger_services` and `set_event_provider` for further setup.
    pub fn new(archivist_configuration: configs::Config) -> Result<Self, Error> {
        let log_manager = logs::LogManager::new(diagnostics::root().create_child("log_stats"));

        let mut fs = ServiceFs::new();
        diagnostics::serve(&mut fs)?;

        let writer = if let Some(archive_path) = &archivist_configuration.archive_path {
            let writer = archive::ArchiveWriter::open(archive_path)?;
            fs.add_remote(
                "archive",
                io_util::open_directory_in_namespace(
                    &archive_path.to_string_lossy(),
                    io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
                )?,
            );
            Some(writer)
        } else {
            None
        };

        // The Inspect Repository offered to the ALL_ACCESS pipeline. This repository is unique
        // in that it has no statically configured selectors, meaning all diagnostics data is visible.
        // This should not be used for production services.
        let all_inspect_repository =
            Arc::new(RwLock::new(inspect::InspectDataRepository::new(None)));

        // TODO(4601): Refactor this code.
        // Set up loading feedback pipeline configs.
        let pipelines_node = diagnostics::root().create_child("pipelines");
        let feedback_pipeline = pipelines_node.create_child("feedback");
        let legacy_pipeline = pipelines_node.create_child("legacy_metrics");
        let feedback_config = configs::PipelineConfig::from_directory("/config/data/feedback");
        feedback_config.record_to_inspect(&feedback_pipeline);
        let legacy_config = configs::PipelineConfig::from_directory("/config/data/legacy_metrics");
        legacy_config.record_to_inspect(&legacy_pipeline);
        // Do not set the state to error if the pipelines simply do not exist.
        let pipeline_exists = !((Path::new("/config/data/feedback").is_dir()
            && feedback_config.has_error())
            || (Path::new("/config/data/legacy_metrics").is_dir() && legacy_config.has_error()));
        if let Some(to_summarize) = &archivist_configuration.summarized_dirs {
            data_stats::add_stats_nodes(component::inspector().root(), to_summarize.clone())?;
        }

        let archivist_state = archive::ArchivistState::new(
            archivist_configuration,
            all_inspect_repository.clone(),
            writer,
        )?;

        fs.dir("svc").add_fidl_service(move |stream| {
            let all_archive_accessor =
                archive_accessor::ArchiveAccessor::new(all_inspect_repository.clone());
            all_archive_accessor.spawn_archive_accessor_server(stream)
        });

        Ok(Self {
            fs,
            state: archivist_state,
            log_sinks: None,
            pipeline_exists,
            _pipeline_nodes: vec![pipelines_node, feedback_pipeline, legacy_pipeline],
            _pipeline_configs: vec![feedback_config, legacy_config],
            log_manager,
            provider: None,
            stop_recv: None,
        })
    }

    /// Returns reference to LogManager.
    pub fn log_manager(&self) -> &logs::LogManager {
        &self.log_manager
    }

    /// Run archivist to completion.
    /// # Arguments:
    /// * `outgoing_channel`- channel to serve outgoing directory on.
    pub async fn run(mut self, outgoing_channel: zx::Channel) -> Result<(), Error> {
        let log_sinks = self.log_sinks.ok_or(format_err!("log services where not installed"))?;
        self.fs.serve_connection(outgoing_channel)?;
        // Start servcing all outgoing services.
        let run_outgoing = self.fs.collect::<()>().map(Ok);
        // collect events.
        let run_event_collection = match self.provider {
            Some(provider) => Either::Left(Self::collect_component_events(
                provider,
                self.state,
                self.pipeline_exists,
            )),
            None => Either::Right(future::ok(())),
        };
        // Process messages from log sink.
        let all_msg = async move {
            log_sinks
                .for_each_concurrent(None, |rx| async move {
                    rx.await;
                })
                .await;
        }
        .map(Ok);

        let (abortable_fut, abort_handle) =
            abortable(future::try_join(run_outgoing, run_event_collection));

        let abortable_fut = abortable_fut.map(|o| {
            if let Ok(r) = o {
                return r;
            } else {
                // discard aborted error
                return Ok(((), ()));
            }
        });

        let stop_fut = match self.stop_recv {
            Some(stop_recv) => Either::Left(async move {
                stop_recv.into_future().await;
                abort_handle.abort();
                Ok(())
            }),
            None => Either::Right(future::ok(())),
        };

        // Combine all three futures into a main future.
        future::try_join3(abortable_fut, stop_fut, all_msg).await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::logs::message::fx_log_packet_t,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_diagnostics_test::ControllerMarker,
        fidl_fuchsia_io as fio,
        fidl_fuchsia_logger::{
            LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogSinkMarker, LogSinkProxy,
        },
        fio::DirectoryProxy,
        fuchsia_async as fasync,
        fuchsia_component::client::connect_to_protocol_at_dir,
        fuchsia_syslog_listener::{run_log_listener_with_proxy, LogProcessor},
        futures::channel::oneshot,
    };

    /// Helper to connec tot log sink and make it easy to write logs to socket.
    struct LogSinkHelper {
        log_sink: Option<LogSinkProxy>,
        sock: Option<zx::Socket>,
    }

    impl LogSinkHelper {
        fn new(directory: &DirectoryProxy) -> Self {
            let log_sink = connect_to_protocol_at_dir::<LogSinkMarker>(&directory)
                .expect("cannot connect to log sink");
            let mut s = Self { log_sink: Some(log_sink), sock: None };
            s.sock = Some(s.connect());
            return s;
        }

        fn connect(&self) -> zx::Socket {
            let (sin, sout) =
                zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("Cannot create socket");
            self.log_sink
                .as_ref()
                .unwrap()
                .connect(sin)
                .expect("unable to send socket to log sink");
            return sout;
        }
        /// kills current sock and creates new connection.
        fn add_new_connection(&mut self) {
            self.kill_sock();
            self.sock = Some(self.connect());
        }

        fn kill_sock(&mut self) {
            self.sock.take();
        }

        fn write_log(&self, msg: &str) {
            Self::write_log_at(self.sock.as_ref().unwrap(), msg);
        }

        fn write_log_at(sock: &zx::Socket, msg: &str) {
            let mut p: fx_log_packet_t = Default::default();
            p.metadata.pid = 1;
            p.metadata.tid = 1;
            p.metadata.severity = LogLevelFilter::Info.into_primitive().into();
            p.metadata.dropped_logs = 0;
            p.data[0] = 0;
            p.add_data(1, msg.as_bytes());

            sock.write(&mut p.as_bytes()).unwrap();
        }

        fn kill_log_sink(&mut self) {
            self.log_sink.take();
        }
    }

    struct Listener {
        send_logs: mpsc::UnboundedSender<String>,
    }

    impl LogProcessor for Listener {
        fn log(&mut self, message: LogMessage) {
            self.send_logs.unbounded_send(message.msg).unwrap();
        }

        fn done(&mut self) {
            panic!("this should not be called");
        }
    }

    fn init_archivist() -> Archivist {
        let config = configs::Config {
            archive_path: None,
            max_archive_size_bytes: 10,
            max_event_group_size_bytes: 10,
            num_threads: 1,
            summarized_dirs: None,
        };

        Archivist::new(config).unwrap()
    }

    // run archivist and send signal when it dies.
    fn run_archivist_and_signal_on_exit() -> (DirectoryProxy, oneshot::Receiver<()>) {
        let (directory, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut archivist = init_archivist();
        archivist.install_logger_services().install_controller_service();
        let (signal_send, signal_recv) = oneshot::channel();
        fasync::spawn(async move {
            archivist.run(server_end.into_channel()).await.expect("Cannot run archivist");
            signal_send.send(()).unwrap();
        });
        (directory, signal_recv)
    }

    // runs archivist and returns its directory.
    fn run_archivist() -> DirectoryProxy {
        let (directory, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut archivist = init_archivist();
        archivist.install_logger_services();
        fasync::spawn(async move {
            archivist.run(server_end.into_channel()).await.expect("Cannot run archivist");
        });
        directory
    }

    fn start_listener(directory: &DirectoryProxy) -> mpsc::UnboundedReceiver<String> {
        let log_proxy = connect_to_protocol_at_dir::<LogMarker>(&directory)
            .expect("cannot connect to log proxy");
        let (send_logs, recv_logs) = mpsc::unbounded();
        let mut options = LogFilterOptions {
            filter_by_pid: false,
            pid: 0,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            filter_by_tid: false,
            tid: 0,
            tags: vec![],
        };
        let l = Listener { send_logs };
        fasync::spawn(async move {
            run_log_listener_with_proxy(&log_proxy, l, Some(&mut options), false).await.unwrap();
        });

        return recv_logs;
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_log_and_retrive_log() {
        let directory = run_archivist();
        let mut recv_logs = start_listener(&directory);

        let mut log_helper = LogSinkHelper::new(&directory);
        log_helper.write_log("my msg1");
        log_helper.write_log("my msg2");

        assert_eq!(
            vec! {Some("my msg1".to_owned()),Some("my msg2".to_owned())},
            vec! {recv_logs.next().await,recv_logs.next().await}
        );

        // new client can log
        let mut log_helper2 = LogSinkHelper::new(&directory);
        log_helper2.write_log("my msg1");
        log_helper.write_log("my msg2");

        let mut expected = vec!["my msg1".to_owned(), "my msg2".to_owned()];
        expected.sort();

        let mut actual = vec![recv_logs.next().await.unwrap(), recv_logs.next().await.unwrap()];
        actual.sort();

        assert_eq!(expected, actual);

        // can log after killing log sink proxy
        log_helper.kill_log_sink();
        log_helper.write_log("my msg1");
        log_helper.write_log("my msg2");

        assert_eq!(
            expected,
            vec! {recv_logs.next().await.unwrap(),recv_logs.next().await.unwrap()}
        );

        // can log from new socket cnonnection
        log_helper2.add_new_connection();
        log_helper2.write_log("my msg1");
        log_helper2.write_log("my msg2");

        assert_eq!(
            expected,
            vec! {recv_logs.next().await.unwrap(),recv_logs.next().await.unwrap()}
        );
    }

    /// Makes sure that implementaion can handle multiple sockets from same
    /// log sink.
    #[fasync::run_singlethreaded(test)]
    async fn log_from_multiple_sock() {
        let directory = run_archivist();
        let mut recv_logs = start_listener(&directory);

        let log_helper = LogSinkHelper::new(&directory);
        let sock1 = log_helper.connect();
        let sock2 = log_helper.connect();
        let sock3 = log_helper.connect();

        LogSinkHelper::write_log_at(&sock1, "msg sock1-1");
        LogSinkHelper::write_log_at(&sock2, "msg sock2-1");
        LogSinkHelper::write_log_at(&sock1, "msg sock1-2");
        LogSinkHelper::write_log_at(&sock3, "msg sock3-1");
        LogSinkHelper::write_log_at(&sock2, "msg sock2-2");

        let mut expected = vec![
            "msg sock1-1".to_owned(),
            "msg sock1-2".to_owned(),
            "msg sock2-1".to_owned(),
            "msg sock2-2".to_owned(),
            "msg sock3-1".to_owned(),
        ];
        expected.sort();

        let mut actual = vec![
            recv_logs.next().await.unwrap(),
            recv_logs.next().await.unwrap(),
            recv_logs.next().await.unwrap(),
            recv_logs.next().await.unwrap(),
            recv_logs.next().await.unwrap(),
        ];
        actual.sort();

        assert_eq!(expected, actual);
    }

    /// Stop API works
    #[fasync::run_singlethreaded(test)]
    async fn stop_works() {
        let (directory, signal_recv) = run_archivist_and_signal_on_exit();
        let mut recv_logs = start_listener(&directory);

        {
            // make sure we can write logs
            let log_sink_helper = LogSinkHelper::new(&directory);
            let sock1 = log_sink_helper.connect();
            LogSinkHelper::write_log_at(&sock1, "msg sock1-1");
            log_sink_helper.write_log("msg sock1-2");
            let mut expected = vec!["msg sock1-1".to_owned(), "msg sock1-2".to_owned()];
            expected.sort();
            let mut actual = vec![recv_logs.next().await.unwrap(), recv_logs.next().await.unwrap()];
            actual.sort();
            assert_eq!(expected, actual);

            //  Start new connections and sockets
            let log_sink_helper1 = LogSinkHelper::new(&directory);
            let sock2 = log_sink_helper.connect();
            // Write logs before calling stop
            log_sink_helper1.write_log("msg 1");
            log_sink_helper1.write_log("msg 2");
            let log_sink_helper2 = LogSinkHelper::new(&directory);

            let controller = connect_to_protocol_at_dir::<ControllerMarker>(&directory)
                .expect("cannot connect to log proxy");
            controller.stop().unwrap();

            // make more socket connections and write to them and old ones.
            let sock3 = log_sink_helper2.connect();
            log_sink_helper2.write_log("msg 3");
            log_sink_helper2.write_log("msg 4");

            LogSinkHelper::write_log_at(&sock3, "msg 5");
            LogSinkHelper::write_log_at(&sock2, "msg 6");
            log_sink_helper.write_log("msg 7");
            LogSinkHelper::write_log_at(&sock1, "msg 8");

            LogSinkHelper::write_log_at(&sock2, "msg 9");
        } // kills all sockets and log_sink connections
        let mut expected = vec![];
        let mut actual = vec![];
        for i in 1..=9 {
            expected.push(format!("msg {}", i));
            actual.push(recv_logs.next().await.unwrap());
        }
        expected.sort();
        actual.sort();

        // make sure archivist is dead.
        signal_recv.await.unwrap();

        assert_eq!(expected, actual);
    }
}
