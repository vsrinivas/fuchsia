// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use anyhow::Error;
use diagnostics_data::{Data, Logs};
use diagnostics_message::{fx_log_packet_t, METADATA_SIZE};
use diagnostics_reader::ArchiveReader;
use fidl::prelude::*;
use fidl_fuchsia_archivist_tests::{
    SocketPuppetControllerRequest, SocketPuppetControllerRequestStream, SocketPuppetProxy,
};
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component::RealmMarker;
use fidl_fuchsia_component_decl::ChildRef;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_io::DirectoryMarker;
use fuchsia_async::Task;
use fuchsia_component::{client, server::ServiceFs};
use fuchsia_component_test::new::{
    Capability, ChildOptions, LocalComponentHandles, RealmInstance, Ref, Route,
};
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc::{self, Receiver},
    StreamExt,
};
use std::ops::Deref;
use tracing::{debug, info};

const TEST_PACKET_LEN: usize = 49;
const MAX_PUPPETS: usize = 5;
const SPAM_PUPPET_ID: usize = 0;
const VICTIM_PUPPET_ID: usize = 1;
const SPAM_COUNT: usize = 9001;

#[fuchsia::test(logging = false)]
async fn test_budget() {
    let _ = diagnostics_log::init_publishing(diagnostics_log::PublishOptions {
        interest: diagnostics_log::Interest {
            min_severity: Some(diagnostics_log::Severity::Debug),
            ..diagnostics_log::Interest::EMPTY
        },
        ..Default::default()
    })
    .unwrap();

    info!("testing that the archivist's log buffers correctly enforce their budget");

    info!("creating nested environment for collecting diagnostics");
    let mut env = PuppetEnv::create(MAX_PUPPETS).await;
    // New test
    // Spam puppet which spams the good puppet's logs removing them from the buffer
    env.create_puppet(SPAM_PUPPET_ID).await;
    env.create_puppet(VICTIM_PUPPET_ID).await;
    let expected = env.running_puppets[VICTIM_PUPPET_ID].emit_packet().await;
    let mut observed_logs = env.log_reader.snapshot_then_subscribe::<Logs>().unwrap();
    let msg_a = observed_logs.next().await.unwrap().unwrap();
    assert_eq!(expected, msg_a);
    let mut last_msg;
    for _ in 0..SPAM_COUNT {
        last_msg = env.running_puppets[SPAM_PUPPET_ID].emit_packet().await;
        assert_eq!(last_msg, observed_logs.next().await.unwrap().unwrap());
    }
    let mut observed_logs = env.log_reader.snapshot_then_subscribe::<Logs>().unwrap();
    let msg_b = observed_logs.next().await.unwrap().unwrap();
    // First message should have been rolled out
    assert_eq!(msg_b.rolled_out_logs(), Some(8940));
    assert_eq!(observed_logs.next().await.unwrap().unwrap().rolled_out_logs(), None);
    assert_ne!(msg_a, msg_b);
}

struct PuppetEnv {
    max_puppets: usize,
    instance: RealmInstance,
    controllers: Receiver<SocketPuppetControllerRequestStream>,
    launched_monikers: Vec<String>,
    running_puppets: Vec<Puppet>,
    log_reader: ArchiveReader,
    _log_errors: Task<()>,
}

impl PuppetEnv {
    async fn create(max_puppets: usize) -> Self {
        let (sender, controllers) = mpsc::channel(1);
        let (builder, test_realm) = test_topology::create(test_topology::Options {
            archivist_url: ARCHIVIST_WITH_SMALL_CACHES,
        })
        .await
        .expect("create base topology");
        let mocks_server = builder
            .add_local_child(
                "mocks-server",
                move |handles: LocalComponentHandles| Box::pin(run_mocks(handles, sender.clone())),
                ChildOptions::new(),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.archivist.tests.SocketPuppetController",
                    ))
                    .from(&mocks_server)
                    .to(&test_realm),
            )
            .await
            .unwrap();

        for i in 0..max_puppets {
            let name = format!("puppet-{}", i);
            let puppet = test_realm
                .add_child(name.clone(), SOCKET_PUPPET_COMPONENT_URL, ChildOptions::new())
                .await
                .unwrap();
            test_realm
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(
                            "fuchsia.archivist.tests.SocketPuppetController",
                        ))
                        .from(Ref::parent())
                        .to(&puppet),
                )
                .await
                .unwrap();
            test_realm
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                        .from(Ref::child("archivist"))
                        .to(&puppet),
                )
                .await
                .unwrap();
        }

        info!("starting our instance");
        test_topology::expose_test_realm_protocol(&builder, &test_realm).await;
        let instance = builder.build().await.expect("create instance");

        let archive =
            || instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
        let mut inspect_reader = ArchiveReader::new();
        inspect_reader
            .with_archive(archive())
            .with_minimum_schema_count(1) // we only request inspect from our archivist
            .add_selector("archivist:root/logs_buffer")
            .add_selector("archivist:root/sources");
        let mut log_reader = ArchiveReader::new();
        log_reader
            .with_archive(archive())
            .with_minimum_schema_count(0) // we want this to return even when no log messages
            .retry_if_empty(false);
        let (_log_subscription, mut errors) =
            log_reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();

        let _log_errors = Task::spawn(async move {
            if let Some(error) = errors.next().await {
                panic!("{:#?}", error);
            }
        });

        Self {
            max_puppets,
            controllers,
            instance,
            launched_monikers: vec![],
            running_puppets: vec![],
            log_reader,
            _log_errors,
        }
    }

    async fn create_puppet(&mut self, id: usize) -> String {
        assert!(id < self.max_puppets);
        let mut child_ref = ChildRef { name: format!("puppet-{}", id), collection: None };

        let (exposed_dir, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let realm = self.instance.root.connect_to_protocol_at_exposed_dir::<RealmMarker>().unwrap();
        realm.open_exposed_dir(&mut child_ref, server_end).await.unwrap().unwrap();

        let _ = client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir)
            .unwrap();

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

        let moniker =
            format!("realm_builder:{}/test/puppet-{}", self.instance.root.child_name(), id);
        let puppet = Puppet { moniker: moniker.clone(), proxy };

        info!("having the puppet connect to LogSink");
        puppet.connect_to_log_sink().await.unwrap();

        info!("observe the puppet appears in archivist's inspect output");
        self.launched_monikers.push(moniker.clone());
        self.running_puppets.push(puppet);
        moniker
    }
}

struct Puppet {
    proxy: SocketPuppetProxy,
    moniker: String,
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

async fn run_mocks(
    handles: LocalComponentHandles,
    mut sender: mpsc::Sender<SocketPuppetControllerRequestStream>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream: SocketPuppetControllerRequestStream| {
        sender.start_send(stream).unwrap();
    });
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct Count {
    total: usize,
    rolled_out: usize,
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
