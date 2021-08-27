// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants, logs::utils::Listener, test_topology};
use diagnostics_reader::{ArchiveReader, Logs};
use fidl_fuchsia_archivist_tests::StdioPuppetMarker;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogProxy};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_component_test::builder::{Capability, CapabilityRoute, RouteEndpoint};
use fuchsia_syslog::{self as syslog, fx_log_info};
use fuchsia_syslog_listener as syslog_listener;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, Stream, StreamExt};
use tracing::warn;

fn run_listener(tag: &str, proxy: LogProxy) -> impl Stream<Item = LogMessage> {
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
    fasync::Task::spawn(async move {
        let fut = syslog_listener::run_log_listener_with_proxy(
            &proxy,
            l,
            Some(&mut options),
            false,
            None,
        );
        if let Err(e) = fut.await {
            panic!("test fail {:?}", e);
        }
    })
    .detach();
    recv_logs
}

#[fasync::run_singlethreaded(test)]
async fn listen_for_syslog() {
    let random = rand::random::<u16>();
    let tag = "logger_integration_rust".to_string() + &random.to_string();
    syslog::init_with_tags(&[&tag]).expect("should not fail");
    let log_proxy = client::connect_to_protocol::<LogMarker>().unwrap();
    let incoming = run_listener(&tag, log_proxy);
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
    assert_eq!(logs[1].msg, "log crate: 20 ");
}

#[fuchsia::test]
async fn listen_for_klog() {
    let builder = test_topology::create(test_topology::Options {
        archivist_url: constants::ARCHIVIST_WITH_KLOG_URL,
    })
    .await
    .expect("create base topology");
    let instance = builder.build().create().await.expect("create instance");
    let log_proxy = instance.root.connect_to_protocol_at_exposed_dir::<LogMarker>().unwrap();
    let logs = run_listener("klog", log_proxy);

    let msg = format!("logger_integration_rust test_klog {}", rand::random::<u64>());

    let resource = zx::Resource::from(zx::Handle::invalid());
    let debuglog = zx::DebugLog::create(&resource, zx::DebugLogOpts::empty()).unwrap();
    debuglog.write(msg.as_bytes()).unwrap();

    logs.filter(|m| futures::future::ready(m.msg == msg)).next().await;
}

#[fuchsia::test]
async fn listen_for_syslog_routed_stdio() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_eager_component(&mut builder, "stdio-puppet", constants::STDIO_PUPPET_URL)
        .await
        .expect("add child");
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.archivist.tests.StdioPuppet"),
            source: RouteEndpoint::component("test/stdio-puppet"),
            targets: vec![RouteEndpoint::AboveRoot],
        })
        .unwrap();
    let instance = builder.build().create().await.expect("create instance");

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let mut reader = ArchiveReader::new();
    reader.with_archive(accessor);
    let (mut logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _errors = fasync::Task::spawn(async move {
        while let Some(e) = errors.next().await {
            panic!("error in subscription: {}", e);
        }
    });

    let puppet = instance.root.connect_to_protocol_at_exposed_dir::<StdioPuppetMarker>().unwrap();

    let msg = format!("logger_integration_rust test_klog stdout {}", rand::random::<u64>());
    puppet.writeln_stdout(&msg).unwrap();
    logs.by_ref().filter(|m| futures::future::ready(m.msg().unwrap() == msg)).next().await;

    let msg = format!("logger_integration_rust test_klog stderr {}", rand::random::<u64>());
    puppet.writeln_stderr(&msg).unwrap();
    logs.filter(|m| futures::future::ready(m.msg().unwrap() == msg)).next().await;

    // TODO(fxbug.dev/49357): add test for multiline log once behavior is defined.
}
