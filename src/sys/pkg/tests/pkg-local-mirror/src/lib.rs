// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests use pseudo_directory, which can exceed the recursion limit.
// We could have refactored the tests to not hit the limit. Instead, we choose to optimize
// for test readability and increase the recursion limit.
#![recursion_limit = "512"]
#![warn(clippy::all)]

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::{LocalMirrorMarker, LocalMirrorProxy},
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
    },
    fuchsia_url::RepositoryUrl,
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

        let builder = RealmBuilder::new().await.expect("created");

        // Create the component-under-test (pkg-local-mirror) child component.
        // This is the production component + manifest.
        let component_under_test = builder
            .add_child(COMPONENT_UNDER_TEST, PKG_LOCAL_MIRROR_URL, ChildOptions::new().eager())
            .await
            .expect("component added");

        // Create a mock component that provides the mock `/usb` directory.
        // The `/usb` directory that is served is configured in this builder.
        let usb_mock = builder
            .add_local_child(
                USB_MOCK_COMPONENT,
                move |h: LocalComponentHandles| {
                    let proxy = spawn_vfs(usb_dir.clone());
                    async move {
                        let _ = &h;
                        let mut fs = ServiceFs::new();
                        fs.add_remote("usb", proxy);
                        fs.serve_connection(h.outgoing_dir).expect("serve mock ServiceFs");
                        fs.collect::<()>().await;
                        Ok::<(), anyhow::Error>(())
                    }
                    .boxed()
                },
                ChildOptions::new(),
            )
            .await
            .expect("mock component added");

        // Route the mock `/usb` directory from the mock source to the component-under-test.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("usb").path("/usb").rights(fio::R_STAR_DIR))
                    .from(&usb_mock)
                    .to(&component_under_test),
            )
            .await
            .expect("usb capability routed");

        // Route the component-under-test's public FIDL protocol so that it is
        // accessible by this test.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.pkg.LocalMirror"))
                    .from(&component_under_test)
                    .to(Ref::parent()),
            )
            .await
            .expect("fuchsia.pkg.LocalMirror routed");

        // Route the logging protocol so that the component-under-test can be
        // debugged.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&component_under_test),
            )
            .await
            .expect("fuchsia.logger.LogSink routed");

        TestEnv { instance: builder.build().await.expect("created") }
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

fn spawn_vfs(dir: Arc<dyn DirectoryEntry>) -> fio::DirectoryProxy {
    let (client_end, server_end) =
        fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
    let scope = vfs::execution_scope::ExecutionScope::new();
    dir.open(
        scope,
        fio::OpenFlags::RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server_end.into_channel()),
    );
    client_end.into_proxy().unwrap()
}

fn repo_url() -> RepositoryUrl {
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
        flags: fio::OpenFlags,
        _mode: u32,
        _path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        assert!(flags.intersects(fio::OpenFlags::DESCRIBE));
        let (_, ch) = server_end.into_stream_and_control_handle().unwrap();

        // Need to send OnOpen because of the Describe flag.
        ch.send_on_open_(
            Status::OK.into_raw(),
            Some(&mut fio::NodeInfoDeprecated::Directory(fio::DirectoryObject)),
        )
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
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
    }
}
