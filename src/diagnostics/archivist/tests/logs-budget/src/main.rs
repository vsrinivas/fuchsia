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
    ops::Deref,
    sync::atomic::{AtomicBool, Ordering},
    time::Duration,
};
use tracing::{debug, info};

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/test-logs-budget#meta/archivist-with-small-caches.cmx";
const PUPPET_URL: &str = "fuchsia-pkg://fuchsia.com/test-logs-budget#meta/socket-puppet.cmx";
const PUPPET_MONIKER: &str = "socket-puppet.cmx";

const TEST_PACKET_LEN: usize = 49;

static HAVE_REQUESTED_STOP: AtomicBool = AtomicBool::new(false);

#[fuchsia_async::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);

    info!("testing that the archivist's log buffers correctly enforce their budget");

    info!("creating nested environment for collecting diagnostics");
    let mut env = PuppetEnv::create().await;

    info!("check that archivist log state is clean");
    env.until_archivist_state_matches(&[]).await;

    let puppet = env.launch_puppet().await;
    env.validate(&puppet).await;

    info!("stopping puppet");
    HAVE_REQUESTED_STOP.store(true, Ordering::SeqCst);
    puppet.stop().await.unwrap();
}

struct PuppetEnv {
    launcher: LauncherProxy,
    controllers: Receiver<SocketPuppetControllerRequestStream>,
    archivist: App,
    messages_allowed_in_cache: usize,
    messages_sent: usize,
    _serve_fs: Task<()>,
}

impl PuppetEnv {
    async fn create() -> Self {
        let (mut sender, controllers) = mpsc::channel(1);
        let mut fs = ServiceFs::new();
        fs.add_fidl_service(move |requests: SocketPuppetControllerRequestStream| {
            debug!("got controller request, forwarding back to main");
            sender.start_send(requests).unwrap();
        });

        let env = fs.create_salted_nested_environment("diagnostics").unwrap();
        let launcher = env.launcher().clone();
        let _serve_fs = Task::spawn(async move {
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

        info!("starting our archivist");
        let archivist =
            launch_with_options(&launcher, ARCHIVIST_URL.to_string(), None, options).unwrap();
        let config = parse_config("/pkg/data/embedding-config.json").unwrap();

        let mut archivist_events = archivist.controller().take_event_stream();
        if let OnTerminated { .. } = archivist_events.next().await.unwrap().unwrap() {
            panic!("archivist terminated early");
        }

        let messages_allowed_in_cache = config.logs.max_cached_original_bytes / TEST_PACKET_LEN;

        Self {
            launcher,
            controllers,
            archivist,
            messages_allowed_in_cache,
            messages_sent: 0,
            _serve_fs,
        }
    }

    fn reader(&self) -> ArchiveReader {
        let archive = self.archivist.connect_to_service::<ArchiveAccessorMarker>().unwrap();
        ArchiveReader::new()
            .with_archive(archive)
            .with_minimum_schema_count(1)
            .add_selector("archivist-with-small-caches.cmx:root/logs_buffer")
            .add_selector("archivist-with-small-caches.cmx:root/sources")
    }

    async fn launch_puppet(&mut self) -> Puppet {
        info!("launching puppet");
        let _app = launch(&self.launcher, PUPPET_URL.to_string(), None).unwrap();

        let mut puppet_events = _app.controller().take_event_stream();
        let _panic_on_exit = Task::spawn(async move {
            if let OnTerminated { .. } = puppet_events.next().await.unwrap().unwrap() {
                if !HAVE_REQUESTED_STOP.load(Ordering::SeqCst) {
                    panic!("puppet terminated early");
                }
            }
        });

        debug!("waiting for controller request");
        let mut controller = self.controllers.next().await.unwrap();

        debug!("waiting for ControlPuppet call");
        let proxy = match controller.next().await {
            Some(Ok(SocketPuppetControllerRequest::ControlPuppet {
                to_control,
                control_handle,
            })) => {
                control_handle.shutdown();
                to_control.into_proxy().unwrap()
            }
            _ => panic!("did not expect that"),
        };

        let puppet = Puppet { _app, moniker: PUPPET_MONIKER.to_owned(), proxy, _panic_on_exit };

        info!("having the puppet connect to LogSink");
        puppet.connect_to_log_sink().await.unwrap();

        info!("observe the puppet appears in archivist's inspect output");
        self.until_archivist_state_matches(&[LogSource {
            moniker: &puppet.moniker,
            messages: Count { total: 0, dropped: 0 },
        }])
        .await;

        puppet
    }

    async fn until_archivist_state_matches(&self, expected_sources: &[LogSource<'_>]) {
        let expected_sources = expected_sources
            .iter()
            .map(|source| (source.moniker.to_string(), source.messages))
            .collect::<BTreeMap<String, Count>>();

        let reader = self.reader();
        loop {
            // we only request inspect from archivist-with-small-caches.cmx, 1 result always returned
            let results = reader.snapshot::<Inspect>().await.unwrap().into_iter().next().unwrap();
            let payload = results.payload.as_ref().unwrap();
            let observed_sources = get_log_counts_by_moniker(&payload);
            if observed_sources == expected_sources {
                break;
            } else {
                debug!("archivist state did not match expected, sleeping");
                debug!("sources observed={:?} expected={:?}", observed_sources, expected_sources);
                Timer::new(Duration::from_millis(100)).await;
            }
        }
    }

    async fn send_message_and_verify(&mut self, puppet: &Puppet) {
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.severity = fuchsia_syslog::levels::INFO;
        packet.fill_data(1..(TEST_PACKET_LEN - METADATA_SIZE), b'A' as _);
        puppet.emit_packet(packet.as_bytes()).await.unwrap();

        self.messages_sent += 1;
        let expected_dropped_count =
            self.messages_sent.saturating_sub(self.messages_allowed_in_cache);
        self.until_archivist_state_matches(&[LogSource {
            moniker: &puppet.moniker,
            messages: Count { total: self.messages_sent, dropped: expected_dropped_count },
        }])
        .await;
    }

    async fn validate(mut self, puppet: &Puppet) {
        info!("having the puppet log packets until overflow");
        for _ in 0..self.messages_allowed_in_cache * 10 {
            self.send_message_and_verify(puppet).await;
        }
    }
}

struct Puppet {
    proxy: SocketPuppetProxy,
    moniker: String,
    _app: App,
    _panic_on_exit: Task<()>,
}

impl Deref for Puppet {
    type Target = SocketPuppetProxy;
    fn deref(&self) -> &Self::Target {
        &self.proxy
    }
}

struct LogSource<'a> {
    moniker: &'a str,
    messages: Count,
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct Count {
    total: usize,
    dropped: usize,
}

fn get_log_counts_by_moniker(root: &DiagnosticsHierarchy) -> BTreeMap<String, Count> {
    let mut counts = BTreeMap::new();
    let sources = root.get_child("sources").unwrap();

    for (moniker, source) in sources.get_children() {
        if let Some(logs) = source.get_child("logs") {
            let total = *logs.get_property("total").unwrap().uint().unwrap() as usize;
            let dropped = *logs.get_property("dropped").unwrap().uint().unwrap() as usize;
            counts.insert(moniker.clone(), Count { total, dropped });
        }
    }

    counts
}
