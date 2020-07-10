// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use archivist_lib::logs::message::fx_log_packet_t;
use fidl::{
    endpoints::{ClientEnd, ServerEnd, ServiceMarker},
    Socket, SocketOpts,
};
use fidl_fuchsia_boot::WriteOnlyLogMarker;
use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogSinkMarker};
use fidl_fuchsia_sys::LauncherMarker;
use fidl_fuchsia_sys_internal::{
    LogConnection, LogConnectionListenerMarker, LogConnectorMarker, LogConnectorRequest,
    LogConnectorRequestStream, SourceIdentity,
};
use fidl_test_log_stdio::StdioPuppetMarker;
use fuchsia_async as fasync;
use fuchsia_component::{
    client::{self as fclient, connect_to_service, launch_with_options, LaunchOptions},
    server::{ServiceFs, ServiceObj},
};
use fuchsia_syslog::{
    self as syslog, fx_log_info,
    levels::{DEBUG, INFO, WARN},
};
use fuchsia_syslog_listener::{self as syslog_listener, run_log_listener_with_proxy, LogProcessor};
use fuchsia_zircon as zx;
use futures::{channel::mpsc, Stream, StreamExt, TryStreamExt};
use log::warn;

struct Listener {
    send_logs: mpsc::UnboundedSender<LogMessage>,
}

impl LogProcessor for Listener {
    fn log(&mut self, message: LogMessage) {
        self.send_logs.unbounded_send(message).unwrap();
    }

    fn done(&mut self) {
        panic!("this should not be called");
    }
}

fn run_listener(tag: &str) -> impl Stream<Item = LogMessage> {
    let mut options = LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        min_severity: LogLevelFilter::None,
        verbosity: 0,
        filter_by_tid: false,
        tid: 0,
        tags: vec![tag.to_string()],
    };
    let (send_logs, recv_logs) = mpsc::unbounded();
    let l = Listener { send_logs };
    fasync::spawn(async move {
        let fut = syslog_listener::run_log_listener(l, Some(&mut options), false, None);
        if let Err(e) = fut.await {
            panic!("test fail {:?}", e);
        }
    });
    recv_logs
}

#[fasync::run_singlethreaded(test)]
async fn listen_for_syslog() {
    let random = rand::random::<u16>();
    let tag = "logger_integration_rust".to_string() + &random.to_string();
    syslog::init_with_tags(&[&tag]).expect("should not fail");
    let incoming = run_listener(&tag);
    fx_log_info!("my msg: {}", 10);
    warn!("log crate: {}", 20);

    let mut logs: Vec<LogMessage> = incoming.take(2).collect().await;

    // sort logs to account for out-of-order arrival
    logs.sort_by(|a, b| a.time.cmp(&b.time));
    assert_eq!(2, logs.len());
    assert_eq!(logs[0].tags, vec![tag.clone()]);
    assert_eq!(logs[0].severity, LogLevelFilter::Info as i32);
    assert_eq!(logs[0].msg, "my msg: 10");

    assert_eq!(logs[1].tags[0], tag.clone());
    assert_eq!(logs[1].severity, LogLevelFilter::Warn as i32);
    assert_eq!(logs[1].msg, "log crate: 20");
}

#[fasync::run_singlethreaded(test)]
async fn listen_for_klog() {
    let logs = run_listener("klog");

    let msg = format!("logger_integration_rust test_klog {}", rand::random::<u64>());

    let resource = zx::Resource::from(zx::Handle::invalid());
    let debuglog = zx::DebugLog::create(&resource, zx::DebugLogOpts::empty()).unwrap();
    debuglog.write(msg.as_bytes()).unwrap();

    logs.filter(|m| futures::future::ready(m.msg == msg)).next().await;
}

#[fasync::run_singlethreaded(test)]
async fn listen_for_klog_routed_stdio() {
    let mut logs = run_listener("klog");

    let app_builder = fclient::AppBuilder::new(
        "fuchsia-pkg://fuchsia.com/archivist_integration_tests#meta/stdio_puppet.cmx",
    );

    let boot_log = connect_to_service::<WriteOnlyLogMarker>().unwrap();
    let stdout_debuglog = boot_log.get().await.unwrap();
    let stderr_debuglog = boot_log.get().await.unwrap();

    let app = app_builder
        .stdout(stdout_debuglog)
        .stderr(stderr_debuglog)
        .spawn(&fclient::launcher().unwrap())
        .unwrap();

    let puppet = app.connect_to_service::<StdioPuppetMarker>().unwrap();

    let msg = format!("logger_integration_rust test_klog stdout {}", rand::random::<u64>());
    puppet.writeln_stdout(&msg).unwrap();
    logs.by_ref().filter(|m| futures::future::ready(m.msg == msg)).next().await;

    let msg = format!("logger_integration_rust test_klog stderr {}", rand::random::<u64>());
    puppet.writeln_stderr(&msg).unwrap();
    logs.filter(|m| futures::future::ready(m.msg == msg)).next().await;

    // TODO(49357): add test for multiline log once behavior is defined.
}

#[fasync::run_singlethreaded(test)]
async fn observer_stop_api() {
    let launcher = connect_to_service::<LauncherMarker>().unwrap();
    // launch observer.cmx
    let mut observer = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx".to_owned(),
        Some(vec!["--disable-log-connector".to_owned()]),
        LaunchOptions::new(),
    )
    .unwrap();

    let log_proxy = observer.connect_to_service::<LogMarker>().unwrap();
    let dir_req = observer.directory_request().clone();
    let mut fs = ServiceFs::<ServiceObj<'_, ()>>::new();

    let (env_proxy, mut logging_component) = fs
        .add_proxy_service_to::<LogSinkMarker, _>(dir_req)
        .launch_component_in_nested_environment(
            "fuchsia-pkg://fuchsia.com/archivist_integration_tests#meta/logging_component.cmx"
                .to_owned(),
            None,
            "test_env",
        )
        .unwrap();
    fasync::spawn(Box::pin(async move {
        fs.collect::<()>().await;
    }));

    let mut options = LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        min_severity: LogLevelFilter::None,
        verbosity: 0,
        filter_by_tid: false,
        tid: 0,
        tags: vec!["logging component".to_owned()],
    };
    let (send_logs, recv_logs) = mpsc::unbounded();
    let l = Listener { send_logs };
    fasync::spawn(async move {
        run_log_listener_with_proxy(&log_proxy, l, Some(&mut options), false, None).await.unwrap();
    });

    // wait for logging_component to die
    assert!(logging_component.wait().await.unwrap().success());

    // kill environment before stopping observer.
    env_proxy.kill().await.unwrap();

    // connect to controller and call stop
    let controller = observer.connect_to_service::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = recv_logs.map(|l| (l.severity, l.msg)).collect::<Vec<_>>().await;

    // recv_logs returned, means observer must be dead. check.
    assert!(observer.wait().await.unwrap().success());
    assert_eq!(
        logs,
        vec![
            (DEBUG, "my debug message.".to_owned()),
            (INFO, "my info message.".to_owned()),
            (WARN, "my warn message.".to_owned()),
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn same_log_sink_simultaneously() {
    // launch observer.cmx
    let launcher = connect_to_service::<LauncherMarker>().unwrap();
    let mut observer = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx".to_owned(),
        Some(vec!["--disable-log-connector".to_owned()]),
        LaunchOptions::new(),
    )
    .unwrap();

    // connect multiple identical log sinks
    for _ in 0..50 {
        let (message_client, message_server) = Socket::create(SocketOpts::DATAGRAM).unwrap();
        let log_sink = observer.connect_to_service::<LogSinkMarker>().unwrap();
        log_sink.connect(message_server).unwrap();

        // each with the same message repeated multiple times
        let mut packet = fx_log_packet_t::default();
        packet.metadata.pid = 1000;
        packet.metadata.tid = 2000;
        packet.metadata.severity = LogLevelFilter::Info.into_primitive().into();
        packet.data[0] = 0;
        packet.add_data(1, "repeated log".as_bytes());
        for _ in 0..5 {
            message_client.write(&mut packet.as_bytes()).unwrap();
        }
    }

    // run log listener
    let log_proxy = observer.connect_to_service::<LogMarker>().unwrap();
    let (send_logs, recv_logs) = mpsc::unbounded();
    fasync::spawn(async move {
        let listen = Listener { send_logs };
        let mut options = LogFilterOptions {
            filter_by_pid: true,
            pid: 1000,
            filter_by_tid: true,
            tid: 2000,
            verbosity: 0,
            min_severity: LogLevelFilter::None,
            tags: Vec::new(),
        };
        run_log_listener_with_proxy(&log_proxy, listen, Some(&mut options), false, None)
            .await
            .unwrap();
    });

    // connect to controller and call stop
    let controller = observer.connect_to_service::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = recv_logs.map(|message| (message.severity, message.msg)).collect::<Vec<_>>().await;

    // recv_logs returned, means observer must be dead. check.
    assert!(observer.wait().await.unwrap().success());
    assert_eq!(
        logs,
        std::iter::repeat((INFO, "repeated log".to_owned())).take(250).collect::<Vec<_>>()
    );
}

#[fasync::run_singlethreaded(test)]
async fn same_log_sink_simultaneously_via_connector() {
    let (client, server) = zx::Channel::create().unwrap();
    let mut serverend = Some(ServerEnd::new(server));

    // connect multiple identical log sinks
    let mut sockets = Vec::new();
    for _ in 0..50 {
        let (message_client, message_server) = Socket::create(SocketOpts::DATAGRAM).unwrap();
        sockets.push(message_server);

        // each with the same message repeated multiple times
        let mut packet = fx_log_packet_t::default();
        packet.metadata.pid = 1000;
        packet.metadata.tid = 2000;
        packet.metadata.severity = LogLevelFilter::Info.into_primitive().into();
        packet.data[0] = 0;
        packet.add_data(1, "repeated log".as_bytes());
        for _ in 0..5 {
            message_client.write(&mut packet.as_bytes()).unwrap();
        }
    }
    {
        let listener = ClientEnd::<LogConnectionListenerMarker>::new(client).into_proxy().unwrap();
        for socket in sockets {
            let (client, server) = zx::Channel::create().unwrap();
            let log_request = ServerEnd::<LogSinkMarker>::new(server);
            let source_identity = SourceIdentity::empty();
            listener
                .on_new_connection(&mut LogConnection { log_request, source_identity })
                .unwrap();
            let log_sink = ClientEnd::<LogSinkMarker>::new(client).into_proxy().unwrap();
            log_sink.connect(socket).unwrap();
        }
    }

    let (dir_client, dir_server) = zx::Channel::create().unwrap();
    let mut fs = ServiceFs::new();
    fs.add_fidl_service_at(
        LogConnectorMarker::NAME,
        move |mut stream: LogConnectorRequestStream| {
            let mut serverend = serverend.take();
            fasync::spawn(async move {
                while let Some(LogConnectorRequest::TakeLogConnectionListener { responder }) =
                    stream.try_next().await.unwrap()
                {
                    responder.send(serverend.take()).unwrap()
                }
            })
        },
    )
    .serve_connection(dir_server)
    .unwrap();
    fasync::spawn(fs.collect());

    // launch observer.cmx
    let launcher = connect_to_service::<LauncherMarker>().unwrap();
    let mut options = LaunchOptions::new();
    options.set_additional_services(vec![LogConnectorMarker::NAME.to_string()], dir_client);
    let mut observer = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx".to_owned(),
        None,
        options,
    )
    .unwrap();

    // run log listener
    let log_proxy = observer.connect_to_service::<LogMarker>().unwrap();
    let (send_logs, recv_logs) = mpsc::unbounded();
    fasync::spawn(async move {
        let listen = Listener { send_logs };
        let mut options = LogFilterOptions {
            filter_by_pid: true,
            pid: 1000,
            filter_by_tid: true,
            tid: 2000,
            verbosity: 0,
            min_severity: LogLevelFilter::None,
            tags: Vec::new(),
        };
        run_log_listener_with_proxy(&log_proxy, listen, Some(&mut options), false, None)
            .await
            .unwrap();
    });

    // connect to controller and call stop
    let controller = observer.connect_to_service::<ControllerMarker>().unwrap();
    controller.stop().unwrap();

    // collect all logs
    let logs = recv_logs.map(|message| (message.severity, message.msg)).collect::<Vec<_>>().await;

    // recv_logs returned, means observer must be dead. check.
    assert!(observer.wait().await.unwrap().success());
    assert_eq!(
        logs,
        std::iter::repeat((INFO, "repeated log".to_owned())).take(250).collect::<Vec<_>>()
    );
}
