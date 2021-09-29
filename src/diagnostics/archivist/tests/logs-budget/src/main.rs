// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use archivist_lib::configs::parse_config;
use diagnostics_data::{Data, LogError, Logs, Severity};
use diagnostics_hierarchy::trie::TrieIterableNode;
use diagnostics_message::message::{fx_log_packet_t, METADATA_SIZE};
use diagnostics_reader::{ArchiveReader, Inspect, SubscriptionResultsStream};
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
use rand::{prelude::SliceRandom, rngs::StdRng, SeedableRng};
use std::{collections::BTreeMap, ops::Deref, time::Duration};
use tracing::{debug, info, trace};

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/test-logs-budget#meta/archivist-with-small-caches.cmx";

const TEST_PACKET_LEN: usize = 49;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);

    info!("testing that the archivist's log buffers correctly enforce their budget");

    info!("creating nested environment for collecting diagnostics");
    let mut env = PuppetEnv::create().await;

    info!("check that archivist log state is clean");
    env.assert_archivist_state_matches_expected().await;

    for i in 0..5 {
        env.launch_puppet(i).await;
    }
    env.validate().await;
}

struct PuppetEnv {
    launcher: LauncherProxy,
    controllers: Receiver<SocketPuppetControllerRequestStream>,
    _archivist: App,
    messages_allowed_in_cache: usize,
    messages_sent: Vec<MessageReceipt>,
    launched_monikers: Vec<String>,
    running_puppets: Vec<Puppet>,
    inspect_reader: ArchiveReader,
    log_reader: ArchiveReader,
    log_subscription: SubscriptionResultsStream<Logs>,
    rng: StdRng,
    _serve_fs: Task<()>,
    _log_errors: Task<()>,
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
        let _archivist =
            launch_with_options(&launcher, ARCHIVIST_URL.to_string(), None, options).unwrap();
        let config = parse_config("/pkg/data/embedding-config.json").unwrap();

        let mut archivist_events = _archivist.controller().take_event_stream();
        if let OnTerminated { .. } = archivist_events.next().await.unwrap().unwrap() {
            panic!("archivist terminated early");
        }

        let messages_allowed_in_cache = config.logs.max_cached_original_bytes / TEST_PACKET_LEN;

        let archive = || _archivist.connect_to_protocol::<ArchiveAccessorMarker>().unwrap();
        let mut inspect_reader = ArchiveReader::new();
        inspect_reader
            .with_archive(archive())
            .with_minimum_schema_count(1) // we only request inspect from our archivist
            .add_selector("archivist-with-small-caches.cmx:root/logs_buffer")
            .add_selector("archivist-with-small-caches.cmx:root/sources");
        let mut log_reader = ArchiveReader::new();
        log_reader
            .with_archive(archive())
            .with_minimum_schema_count(0) // we want this to return even when no log messages
            .retry_if_empty(false);
        let (log_subscription, mut errors) =
            log_reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();

        let _log_errors = Task::spawn(async move {
            if let Some(error) = errors.next().await {
                panic!("{:#?}", error);
            }
        });

        Self {
            launcher,
            controllers,
            _archivist,
            messages_allowed_in_cache,
            messages_sent: vec![],
            launched_monikers: vec![],
            running_puppets: vec![],
            inspect_reader,
            log_reader,
            log_subscription,
            rng: StdRng::seed_from_u64(0xA455),
            _serve_fs,
            _log_errors,
        }
    }

    async fn launch_puppet(&mut self, id: usize) {
        let url =
            format!("fuchsia-pkg://fuchsia.com/test-logs-budget#meta/socket-puppet{}.cmx", id);
        info!(%url, "launching puppet");
        let app = launch(&self.launcher, url, None).unwrap();

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

        let moniker = format!("socket-puppet{}.cmx", id);
        let puppet = Puppet { app, moniker, proxy };

        info!("having the puppet connect to LogSink");
        puppet.connect_to_log_sink().await.unwrap();

        info!("observe the puppet appears in archivist's inspect output");
        self.launched_monikers.push(puppet.moniker.clone());
        self.running_puppets.push(puppet);

        // wait for archivist to catch up with what we launched
        while self.current_expected_sources() != self.current_observed_sources().await {
            Timer::new(Duration::from_millis(100)).await;
        }
    }

    fn current_expected_sources(&self) -> BTreeMap<String, Count> {
        // make sure we have an empty entry for each puppet we've launched
        let mut expected_sources = BTreeMap::new();
        for source in &self.launched_monikers {
            expected_sources.insert(source.clone(), Count { total: 0, dropped: 0 });
        }

        // compute the expected drops for each component based on our list of receipts
        for (prior_messages, receipt) in self.messages_sent.iter().rev().enumerate() {
            let mut puppet_count = expected_sources.get_mut(&receipt.moniker).unwrap();
            puppet_count.total += 1;
            if prior_messages >= self.messages_allowed_in_cache {
                puppet_count.dropped += 1;
            }
        }

        // archivist should have dropped all containers that have stopped and are empty
        expected_sources
            .into_iter()
            .filter(|(moniker, count)| {
                let has_messages = count.total > 0 && count.total != count.dropped;
                let is_running =
                    self.running_puppets.iter().find(|puppet| moniker == &puppet.moniker).is_some();
                is_running || has_messages
            })
            .collect()
    }

    async fn current_observed_sources(&self) -> BTreeMap<String, Count> {
        // we only request inspect from archivist-with-small-caches.cmx, 1 result always returned
        let results =
            self.inspect_reader.snapshot::<Inspect>().await.unwrap().into_iter().next().unwrap();
        let root = results.payload.as_ref().unwrap();

        let mut counts = BTreeMap::new();
        let sources = root.get_child("sources").unwrap();

        for (moniker, source) in sources.get_children() {
            if let Some(logs) = source.get_child("logs") {
                let total = logs.get_child("total").unwrap();
                let total_number = *total.get_property("number").unwrap().uint().unwrap() as usize;
                let total_bytes = *total.get_property("bytes").unwrap().uint().unwrap() as usize;
                assert_eq!(total_bytes, total_number * TEST_PACKET_LEN);

                let dropped = logs.get_child("dropped").unwrap();
                let dropped_number =
                    *dropped.get_property("number").unwrap().uint().unwrap() as usize;
                let dropped_bytes =
                    *dropped.get_property("bytes").unwrap().uint().unwrap() as usize;
                assert_eq!(dropped_bytes, dropped_number * TEST_PACKET_LEN);

                counts.insert(
                    moniker.clone(),
                    Count { total: total_number, dropped: dropped_number },
                );
            }
        }

        counts
    }

    async fn assert_archivist_state_matches_expected(&self) {
        let expected_sources = self.current_expected_sources();
        let observed_sources = self.current_observed_sources().await;
        assert_eq!(observed_sources, expected_sources);

        let expected_drops = || expected_sources.iter().filter(|(_, c)| c.dropped > 0);
        let mut expected_logs = self
            .messages_sent
            .iter()
            .rev() // we want the newest messages
            .take(self.messages_allowed_in_cache)
            .rev(); // but in the order they were sent
        trace!("reading log snapshot");
        let observed_logs = self.log_reader.snapshot::<Logs>().await.unwrap().into_iter();

        let mut dropped_message_warnings = BTreeMap::new();
        for observed in observed_logs {
            if observed.metadata.errors.is_some() {
                let moniker = observed.moniker.split(":").next().unwrap().to_string();
                dropped_message_warnings.insert(moniker, observed);
            } else {
                let expected = expected_logs.next().unwrap();
                assert_eq!(expected, &observed);
            }
        }

        for (moniker, Count { dropped, .. }) in expected_drops() {
            let dropped_logs_warning = dropped_message_warnings.remove(moniker).unwrap();
            assert_eq!(
                dropped_logs_warning.metadata.errors,
                Some(vec![LogError::DroppedLogs { count: *dropped as u64 }])
            );
            assert_eq!(dropped_logs_warning.metadata.severity, Severity::Warn);
        }

        assert!(dropped_message_warnings.is_empty(), "must have encountered all expected warnings");
    }

    async fn validate(mut self) {
        // we want to spend most of this test's effort exercising the behavior of dropping messages
        let overall_messages_to_log = self.messages_allowed_in_cache * 15;

        // we want to ensure that messages are retained after a component stops, up to our policy.
        // this value should be chosen to ensure that we get to a point of rolling out all the
        // messages for the stopped component and actually dropping it
        let iteration_for_killing_a_puppet = self.messages_allowed_in_cache;

        info!("having the puppets log packets until overflow");
        for i in 0..overall_messages_to_log {
            trace!(i, "loop ticked");
            if i == iteration_for_killing_a_puppet {
                let mut to_stop = self.running_puppets.pop().unwrap();
                let receipt = to_stop.emit_packet().await;
                self.check_receipt(receipt).await;

                to_stop.app.kill().unwrap();
                to_stop.app.wait().await.unwrap();
            }

            let puppet = self.running_puppets.choose(&mut self.rng).unwrap();
            let receipt = puppet.emit_packet().await;
            self.check_receipt(receipt).await;
        }

        assert_eq!(
            self.current_expected_sources().len(),
            self.running_puppets.len(),
            "must have stopped a component and rolled out all of its logs"
        );
        info!("test complete!");
    }

    async fn check_receipt(&mut self, receipt: MessageReceipt) {
        let next_message = self.log_subscription.next().await.unwrap();
        assert_eq!(receipt, next_message);

        self.messages_sent.push(receipt);
        self.assert_archivist_state_matches_expected().await;
    }
}

struct Puppet {
    proxy: SocketPuppetProxy,
    moniker: String,
    app: App,
}

impl std::fmt::Debug for Puppet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Puppet").field("moniker", &self.moniker).finish()
    }
}

impl Puppet {
    async fn emit_packet(&self) -> MessageReceipt {
        let timestamp = zx::Time::get_monotonic().into_nanos();
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.severity = fuchsia_syslog::levels::INFO;
        packet.metadata.time = timestamp;
        packet.fill_data(1..(TEST_PACKET_LEN - METADATA_SIZE), b'A' as _);
        self.proxy.emit_packet(packet.as_bytes()).await.unwrap();
        MessageReceipt { timestamp, moniker: self.moniker.clone() }
    }
}

impl Deref for Puppet {
    type Target = SocketPuppetProxy;
    fn deref(&self) -> &Self::Target {
        &self.proxy
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct Count {
    total: usize,
    dropped: usize,
}

/// A value indicating a message was sent by a particular puppet.
#[derive(Clone, Debug, PartialEq)]
struct MessageReceipt {
    moniker: String,
    timestamp: i64,
}

impl PartialEq<Data<Logs>> for MessageReceipt {
    fn eq(&self, other: &Data<Logs>) -> bool {
        // we launch `socket_puppet0.cmx` and store that moniker, but the moniker we get back from
        // archivist looks like `socket_puppet0.cmx:12345`, so we do a prefix match instead of
        // full string equality
        other.moniker.starts_with(&self.moniker)
            && *other.metadata.timestamp as i64 == self.timestamp
    }
}
