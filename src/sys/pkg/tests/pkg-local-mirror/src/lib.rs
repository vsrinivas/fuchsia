// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests use pseudo_directory, which can exceed the recursion limit.
// We could have refactored the tests to not hit the limit. Instead, we choose to optimize
// for test readability and increase the recursion limit.
#![recursion_limit = "512"]

use {
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryObject, DirectoryRequest, DirectoryRequestStream, NodeInfo,
    },
    fidl_fuchsia_pkg::{LocalMirrorMarker, LocalMirrorProxy},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_url::pkg_url::RepoUrl,
    fuchsia_zircon::Status,
    futures::{channel::oneshot, prelude::*},
    std::sync::Arc,
    vfs::{directory::entry::DirectoryEntry, pseudo_directory},
};

mod get_blob;
mod get_metadata;

const PKG_LOCAL_MIRROR_CMX: &str =
    "fuchsia-pkg://fuchsia.com/pkg-local-mirror-integration-tests#meta/pkg-local-mirror.cmx";

struct TestEnvBuilder {
    usb_dir: Option<ClientEnd<DirectoryMarker>>,
}
impl TestEnvBuilder {
    fn new() -> Self {
        Self { usb_dir: None }
    }

    fn usb_dir(mut self, usb_dir: ClientEnd<DirectoryMarker>) -> Self {
        assert_eq!(self.usb_dir, None);
        self.usb_dir = Some(usb_dir);
        self
    }

    fn build(self) -> TestEnv {
        let pkg_local_mirror = AppBuilder::new(PKG_LOCAL_MIRROR_CMX.to_owned())
            .add_handle_to_namespace(
                "/usb".to_owned(),
                self.usb_dir
                    .unwrap_or(spawn_vfs(pseudo_directory! {
                        "0" => pseudo_directory! {
                            "fuchsia_pkg" => pseudo_directory! {
                                "blobs" => pseudo_directory! {},
                                "repository_metadata" => pseudo_directory! {},
                            },
                        },
                    }))
                    .into(),
            );

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();

        let env = fs
            .create_salted_nested_environment("pkg_local_mirror_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        let pkg_local_mirror =
            pkg_local_mirror.spawn(env.launcher()).expect("pkg-local-mirror to launch");

        TestEnv { _env: env, pkg_local_mirror }
    }
}

struct TestEnv {
    _env: NestedEnvironment,
    pkg_local_mirror: App,
}

impl TestEnv {
    fn builder() -> TestEnvBuilder {
        TestEnvBuilder::new()
    }

    /// Opens a connection to the LocalMirror FIDL service.
    fn local_mirror_proxy(&self) -> LocalMirrorProxy {
        self.pkg_local_mirror.connect_to_service::<LocalMirrorMarker>().unwrap()
    }
}

fn spawn_vfs(dir: Arc<vfs::directory::immutable::simple::Simple>) -> ClientEnd<DirectoryMarker> {
    let (client_end, server_end) = fidl::endpoints::create_endpoints::<DirectoryMarker>().unwrap();
    let scope = vfs::execution_scope::ExecutionScope::new();
    dir.open(
        scope,
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        0,
        vfs::path::Path::empty(),
        ServerEnd::new(server_end.into_channel()),
    );
    client_end
}

fn repo_url() -> RepoUrl {
    "fuchsia-pkg://fuchsia.com".parse().unwrap()
}

/// The purpose of this function is to guarantee that channels to usb subdirectories
/// /usb/0/fuchsia_pkg/[repository_metadata|blobs] are closed before pkg-local-mirror makes open
/// calls on them, so that the open calls fail with fidl errors. In practice, this will prevent
/// flakes in the tests.
async fn close_channels_then_notify(
    mut stream: DirectoryRequestStream,
    channels_closed_sender: oneshot::Sender<()>,
) {
    // Handle the initial open.
    match stream.next().await.unwrap().unwrap() {
        DirectoryRequest::Open { object, path, .. } => {
            assert_eq!(path, "0/fuchsia_pkg");
            let mut usb_dir_stream =
                ServerEnd::<DirectoryMarker>::new(object.into_channel()).into_stream().unwrap();

            // Handle opens to the subdirectories.
            for _ in 0..2 {
                if let Some(Ok(req)) = usb_dir_stream.next().await {
                    match req {
                        DirectoryRequest::Open { path, object, flags, .. } => {
                            assert!(&path == "blobs" || &path == "repository_metadata");
                            assert!(flags & fidl_fuchsia_io::OPEN_FLAG_DESCRIBE != 0);
                            let (_, ch) = object.into_stream_and_control_handle().unwrap();

                            // Need to send OnOpen because of the Describe flag.
                            ch.send_on_open_(
                                Status::OK.into_raw(),
                                Some(&mut NodeInfo::Directory(DirectoryObject)),
                            )
                            .unwrap();
                        }
                        other => panic!("unhandled inner request type: {:?}", other),
                    }
                }
            }
        }
        other => panic!("unhandled outer request type: {:?}", other),
    }

    // At this point, we guarantee the channels connecting us to subdirectories are closed.
    let () = channels_closed_sender.send(()).unwrap();
}
