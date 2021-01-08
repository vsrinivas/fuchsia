// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use archivist_lib::{
    configs::parse_config,
    logs::message::{fx_log_packet_t, METADATA_SIZE},
};
use diagnostics_hierarchy::{trie::TrieIterableNode, DiagnosticsHierarchy};
use diagnostics_reader::{ArchiveReader, Inspect};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_logger::LogSinkMarker;
use fidl_fuchsia_sys::{ComponentControllerEvent::OnTerminated, LauncherProxy};
use fidl_test_logs_budget::{
    SocketPuppetControllerRequest, SocketPuppetControllerRequestStream, SocketPuppetProxy,
};
use fuchsia_async::{Task, Timer};
use fuchsia_component::{
    client::{launch, launch_with_options, App, LaunchOptions},
    server::ServiceFs,
};
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc::{self, Receiver},
    StreamExt,
};
use std::{
    collections::BTreeMap,
    sync::atomic::{AtomicBool, Ordering},
    time::Duration,
};
use tracing::{debug, info};

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/test-logs-budget#meta/archivist-with-small-caches.cmx";
const PUPPET_URL: &str = "fuchsia-pkg://fuchsia.com/test-logs-budget#meta/socket-puppet.cmx";

static HAVE_REQUESTED_STOP: AtomicBool = AtomicBool::new(false);

#[fuchsia_async::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);

    info!("testing that the archivist's log buffers correctly enforce their budget");

    info!("creating nested environment for collecting diagnostics");
    let (launcher, mut controllers) = create_diagnostics_env();

    info!("starting our archivist");
    let archivist = launch_archivist(&launcher).await;
    let archivist_config = parse_config("/pkg/data/small-caches-config.json").unwrap();
    let archive = archivist.connect_to_service::<ArchiveAccessorMarker>().unwrap();
    let reader = ArchiveReader::new()
        .with_archive(archive)
        .with_minimum_schema_count(1)
        .add_selector("archivist-with-small-caches.cmx:root/logs_buffer")
        .add_selector("archivist-with-small-caches.cmx:root/sources");

    info!("check that archivist log state is clean");
    until_archivist_state_matches(&reader, DroppedLogs(0), &[]).await;

    info!("launching puppet");
    let _puppet_app = launch_puppet(&launcher);

    debug!("waiting for controller request");
    let controller = controllers.next().await.unwrap();
    debug!("waiting for ControlPuppet call");
    let puppet = serve_controller(controller).await;

    info!("having the puppet connect to LogSink");
    puppet.connect_to_log_sink().await.unwrap();

    info!("observe the puppet appears in archivist's inspect output");
    until_archivist_state_matches(
        &reader,
        DroppedLogs(0),
        &[LogSource { url: PUPPET_URL, messages: 0 }],
    )
    .await;

    info!("having the puppet log packets until overflow");
    let packet_len = 49;
    let messages_allowed_in_cache = archivist_config.logs.max_cached_original_bytes / packet_len;
    let messages_to_send = messages_allowed_in_cache as u64 * 10;
    for messages_sent in 1..messages_to_send {
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.severity = fuchsia_syslog::levels::INFO;
        packet.fill_data(1..(packet_len - METADATA_SIZE), b'A' as _);
        puppet.emit_packet(packet.as_bytes()).await.unwrap();

        let expected_dropped_count = messages_sent.saturating_sub(messages_allowed_in_cache as u64);
        until_archivist_state_matches(
            &reader,
            DroppedLogs(expected_dropped_count),
            &[LogSource { url: PUPPET_URL, messages: messages_sent }],
        )
        .await;
    }

    info!("stopping puppet");
    HAVE_REQUESTED_STOP.store(true, Ordering::SeqCst);
    puppet.stop().await.unwrap();
}

fn create_diagnostics_env() -> (LauncherProxy, Receiver<SocketPuppetControllerRequestStream>) {
    let (mut sender, receiver) = mpsc::channel(1);
    let mut fs = ServiceFs::new();
    fs.add_fidl_service(move |requests: SocketPuppetControllerRequestStream| {
        debug!("got controller request, forwarding back to main");
        sender.start_send(requests).unwrap();
    });

    let env = fs.create_salted_nested_environment("diagnostics").unwrap();
    let launcher = env.launcher().clone();
    Task::spawn(async move {
        let _env = env; // move env into the task so it stays alive
        fs.collect::<()>().await
    })
    .detach();
    (launcher, receiver)
}

async fn launch_archivist(launcher: &LauncherProxy) -> App {
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

    let archivist =
        launch_with_options(&launcher, ARCHIVIST_URL.to_string(), None, options).unwrap();

    let mut archivist_events = archivist.controller().take_event_stream();
    if let OnTerminated { .. } = archivist_events.next().await.unwrap().unwrap() {
        panic!("archivist terminated early");
    }

    archivist
}

fn launch_puppet(launcher: &LauncherProxy) -> App {
    let puppet_app = launch(&launcher, PUPPET_URL.to_string(), None).unwrap();

    let mut puppet_events = puppet_app.controller().take_event_stream();
    Task::spawn(async move {
        if let OnTerminated { .. } = puppet_events.next().await.unwrap().unwrap() {
            if !HAVE_REQUESTED_STOP.load(Ordering::SeqCst) {
                panic!("puppet terminated early");
            }
        }
    })
    .detach();

    puppet_app
}

async fn serve_controller(
    mut controller: SocketPuppetControllerRequestStream,
) -> SocketPuppetProxy {
    loop {
        match controller.next().await {
            Some(Ok(SocketPuppetControllerRequest::ControlPuppet {
                to_control,
                control_handle,
            })) => {
                control_handle.shutdown();
                break to_control.into_proxy().unwrap();
            }
            _ => panic!("did not expect that"),
        }
    }
}

struct DroppedLogs(u64);

struct LogSource {
    url: &'static str,
    messages: u64,
}

async fn until_archivist_state_matches(
    reader: &ArchiveReader,
    DroppedLogs(expected_dropped_count): DroppedLogs,
    expected_sources: &[LogSource],
) {
    let expected_sources = expected_sources
        .iter()
        .map(|source| (source.url.to_string(), source.messages))
        .collect::<BTreeMap<String, u64>>();

    loop {
        // we only request inspect from archivist-with-small-caches.cmx, 1 result always returned
        let results = reader.snapshot::<Inspect>().await.unwrap().into_iter().next().unwrap();
        let payload = results.payload.as_ref().unwrap();

        let observed_dropped_count = get_dropped_logs(&payload);
        let observed_sources = get_log_counts_by_url(&payload);

        if observed_dropped_count == expected_dropped_count && observed_sources == expected_sources
        {
            break;
        } else {
            debug!("archivist state did not match expected, sleeping");
            debug!(
                "dropped observed={} expected={}",
                observed_dropped_count, expected_dropped_count
            );
            debug!("sources observed={:?} expected={:?}", observed_sources, expected_sources);
        }

        Timer::new(Duration::from_millis(100)).await;
    }
}

fn get_dropped_logs(root: &DiagnosticsHierarchy) -> u64 {
    *root
        .get_child("logs_buffer")
        .unwrap()
        .get_property("rolled_out_entries")
        .unwrap()
        .uint()
        .unwrap()
}

fn get_log_counts_by_url(root: &DiagnosticsHierarchy) -> BTreeMap<String, u64> {
    let mut counts = BTreeMap::new();
    let sources = root.get_child("sources").unwrap();

    for (_moniker, source) in sources.get_children() {
        if let Some(logs) = source.get_child("logs") {
            let url = source.get_property("url").unwrap().string().unwrap().to_string();
            let total = *logs.get_property("total").unwrap().uint().unwrap();
            counts.insert(url, total);
        }
    }

    counts
}
