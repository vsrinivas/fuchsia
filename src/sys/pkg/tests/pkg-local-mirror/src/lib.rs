// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests use pseudo_directory, which can exceed the recursion limit.
// We could have refactored the tests to not hit the limit. Instead, we choose to optimize
// for test readability and increase the recursion limit.
#![recursion_limit = "512"]

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        self as fio, DirectoryMarker, DirectoryObject, DirectoryProxy, NodeInfo, NodeMarker,
    },
    fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_pkg::{LocalMirrorMarker, LocalMirrorProxy},
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{builder::*, mock::MockHandles, RealmInstance},
    fuchsia_url::pkg_url::RepoUrl,
    fuchsia_zircon::Status,
    futures::{channel::oneshot, prelude::*},
    parking_lot::Mutex,
    std::sync::Arc,
    vfs::{
        directory::entry::{DirectoryEntry, EntryInfo},
        execution_scope::ExecutionScope,
        path::Path,
        pseudo_directory,
    },
};

mod get_blob;
mod get_metadata;

const PKG_LOCAL_MIRROR_URL: &str =
    "fuchsia-pkg://fuchsia.com/pkg-local-mirror-integration-tests#meta/pkg-local-mirror.cm";

struct TestEnvBuilder {
    usb_dir: Option<Arc<dyn DirectoryEntry>>,
}

impl TestEnvBuilder {
    fn new() -> Self {
        Self { usb_dir: None }
    }

    fn usb_dir(mut self, usb_dir: Arc<dyn DirectoryEntry>) -> Self {
        if self.usb_dir.is_some() {
            panic!("USB dir is already set");
        }
        self.usb_dir = Some(usb_dir);
        self
    }

    async fn build(self) -> TestEnv {
        let usb_dir = self.usb_dir.unwrap_or_else(|| {
            // Default pseudo-directory structure.
            pseudo_directory! {
                "0" => pseudo_directory! {
                    "fuchsia_pkg" => pseudo_directory! {
                        "blobs" => pseudo_directory! {},
                        "repository_metadata" => pseudo_directory! {},
                    },
                },
            }
        });

        const COMPONENT_UNDER_TEST: &str = "pkg-local-mirror";
        const USB_MOCK_COMPONENT: &str = "usb-source";

        let mut builder = RealmBuilder::new().await.expect("created");

        // Create the component-under-test (pkg-local-mirror) child component.
        // This is the production component + manifest.
        builder
            .add_eager_component(COMPONENT_UNDER_TEST, ComponentSource::url(PKG_LOCAL_MIRROR_URL))
            .await
            .expect("component added");

        // Create a mock component that provides the mock `/usb` directory.
        // The `/usb` directory that is served is configured in this builder.
        builder
            .add_component(
                USB_MOCK_COMPONENT,
                ComponentSource::mock(move |h: MockHandles| {
                    let proxy = spawn_vfs(usb_dir.clone());
                    async move {
                        let mut fs = ServiceFs::new();
                        fs.add_remote("usb", proxy);
                        fs.serve_connection(h.outgoing_dir.into_channel())
                            .expect("serve mock ServiceFs");
                        fs.collect::<()>().await;
                        Ok::<(), anyhow::Error>(())
                    }
                    .boxed()
                }),
            )
            .await
            .expect("mock component added");

        // Route the mock `/usb` directory from the mock source to the component-under-test.
        builder
            .add_route(CapabilityRoute {
                capability: Capability::directory("usb", "/usb", fio2::R_STAR_DIR),
                source: RouteEndpoint::component(USB_MOCK_COMPONENT),
                targets: vec![RouteEndpoint::component(COMPONENT_UNDER_TEST)],
            })
            .expect("usb capability routed");

        // Route the component-under-test's public FIDL protocol so that it is
        // accessible by this test.
        builder
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.pkg.LocalMirror"),
                source: RouteEndpoint::component(COMPONENT_UNDER_TEST),
                targets: vec![RouteEndpoint::AboveRoot],
            })
            .expect("fuchsia.pkg.LocalMirror routed");

        // Route the logging protocol so that the component-under-test can be
        // debugged.
        builder
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.logger.LogSink"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component(COMPONENT_UNDER_TEST)],
            })
            .expect("fuchsia.logger.LogSink routed");

        TestEnv { instance: builder.build().create().await.expect("created") }
    }
}

struct TestEnv {
    instance: RealmInstance,
}

impl TestEnv {
    fn builder() -> TestEnvBuilder {
        TestEnvBuilder::new()
    }

    /// Opens a connection to the LocalMirror FIDL service.
    fn local_mirror_proxy(&self) -> LocalMirrorProxy {
        self.instance.root.connect_to_protocol_at_exposed_dir::<LocalMirrorMarker>().unwrap()
    }
}

fn spawn_vfs(dir: Arc<dyn DirectoryEntry>) -> DirectoryProxy {
    let (client_end, server_end) = fidl::endpoints::create_endpoints::<DirectoryMarker>().unwrap();
    let scope = vfs::execution_scope::ExecutionScope::new();
    dir.open(
        scope,
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server_end.into_channel()),
    );
    client_end.into_proxy().unwrap()
}

fn repo_url() -> RepoUrl {
    "fuchsia-pkg://fuchsia.com".parse().unwrap()
}

/// The purpose of this DirectoryEntry impl (for use with vfs) is to guarantee that channels to usb
/// subdirectories /usb/0/fuchsia_pkg/[repository_metadata|blobs] are closed before
/// pkg-local-mirror makes open calls on them, so that the open calls fail with fidl errors.
/// In practice, this will prevent flakes in the tests.
struct DropAndSignal(Mutex<Option<oneshot::Sender<()>>>);

impl DropAndSignal {
    /// Creates a new `Arc<dyn DirectoryEntry>` which when opened, reports a successful open event
    /// on the pipelined channel before dropping it. `closed_sender` is signaled, guaranteeing to
    /// the caller that the directory is no longer open once the signal is received.
    fn new(closed_sender: oneshot::Sender<()>) -> Arc<DropAndSignal> {
        Arc::new(DropAndSignal(Mutex::new(Some(closed_sender))))
    }
}

impl DirectoryEntry for DropAndSignal {
    fn open(
        self: Arc<Self>,
        _scope: ExecutionScope,
        flags: u32,
        _mode: u32,
        _path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        assert!(flags & fio::OPEN_FLAG_DESCRIBE != 0);
        let (_, ch) = server_end.into_stream_and_control_handle().unwrap();

        // Need to send OnOpen because of the Describe flag.
        ch.send_on_open_(Status::OK.into_raw(), Some(&mut NodeInfo::Directory(DirectoryObject)))
            .unwrap();

        // Make sure the connection is dropped before signalling.
        drop(ch);

        match self.0.lock().take() {
            Some(sender) => {
                // Signal that the connection to this directory is dropped.
                sender.send(()).unwrap();
            }
            None => {
                panic!("already signaled");
            }
        }
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DIRENT_TYPE_DIRECTORY)
    }
}
