// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::*,
    fidl_fuchsia_io::{self as fio, DirectoryMarker, DirectoryRequest, NodeMarker},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::*,
    fuchsia_runtime,
    fuchsia_zircon::{self as zx},
    futures::prelude::*,
    std::path::{Path, PathBuf},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Setup pkgfs at the out directory of this component
    let startup_handle =
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest.into())
            .expect("missing directory request handle");

    let startup_handle = ServerEnd::new(zx::Channel::from(startup_handle));
    FakePkgfs::new(startup_handle).expect("failed to serve fake pkgfs");

    // Bind to the echo_server.
    let mut child_ref = fsys::ChildRef { name: "echo_server".to_string(), collection: None };
    let (_dir_proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
    let realm_proxy = connect_to_service::<fsys::RealmMarker>()?;
    realm_proxy.bind_child(&mut child_ref, server_end).await?.expect("could not bind to child");

    // Wait indefinitely
    fasync::futures::future::pending::<()>().await;
    panic!("This is an unreachable statement!");
}

// Simulate a fake pkgfs Directory service that only contains a single package, "base_resolver_test".
// This fake is more complex than the one in fuchsia_base_pkg_resolver.rs because it needs to
// support an intermediate Directory.Open call, to open the 'pkgfs root' when initializing the
// resolver, rather than a single Open straight to the faked package. The fake therefore has two modes:
//   1. FakePkgfs::new will initially support opening path "pkgfs/", and handles that open by creating a
//   new fake connection of the 2nd mode.
//   2. The 2nd mode fakes a connection inside the pkgfs root directory, and thus supports opening
//   path "packages/base_resolver_test/0/". It handles that open by connecting to this component's own real
//   package directory.
// TODO(fxb/37534): This is implemented by manually handling the Directory.Open and forwarding to
// the test's real package directory because Rust vfs does not yet support OPEN_RIGHT_EXECUTABLE.
// Simplify in the future.
struct FakePkgfs;

impl FakePkgfs {
    pub fn new(server_end: ServerEnd<DirectoryMarker>) -> Result<(), Error> {
        Self::new_internal(false, server_end)
    }

    fn new_internal(
        inside_pkgfs: bool,
        server_end: ServerEnd<DirectoryMarker>,
    ) -> Result<(), Error> {
        let mut stream = server_end.into_stream().expect("failed to create stream");
        fasync::Task::local(async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    DirectoryRequest::Open { flags, mode: _, path, object, control_handle: _ } => {
                        Self::handle_open(inside_pkgfs, &path, flags, object)
                    }
                    DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                        if flags != fio::CLONE_FLAG_SAME_RIGHTS {
                            panic!(
                                "Fake doesn't support these flags in Directory.Clone: {:#x}",
                                flags
                            )
                        }

                        // Create a new connection of the same type as the current connection.
                        Self::new_internal(inside_pkgfs, ServerEnd::new(object.into_channel()))
                            .unwrap();
                    }
                    _ => panic!("Fake doesn't support request: {:?}", request),
                }
            }
        })
        .detach();
        Ok(())
    }

    fn handle_open(inside_pkgfs: bool, path: &str, flags: u32, server_end: ServerEnd<NodeMarker>) {
        // Only support opening the base_resolver_test package's directory. All other paths just
        // drop the server_end.
        let path = Path::new(path);
        let mut path_iter = path.iter();

        // If the fake connection is in 'inside_pkg' mode, only support opening the
        // base_resolver_test package's directory. Otherwise, only support opening 'pkgfs/'. All
        // other paths just drop the server_end so the client observes PEER_CLOSED>
        let (expected_path, expected_len) = match inside_pkgfs {
            false => (Path::new("pkgfs/"), 1),
            true => (Path::new("packages/base_resolver_test/0"), 3),
        };
        if path_iter.by_ref().take(expected_len).cmp(expected_path.iter())
            != std::cmp::Ordering::Equal
        {
            return;
        }

        if inside_pkgfs {
            // Connect the server_end by forwarding to our real package directory, which can handle
            // OPEN_RIGHT_EXECUTABLE. Also, pass through the input flags here to ensure that we don't
            // artificially elevate rights (i.e. the resolver needs to ask for the appropriate rights).
            let mut open_path = PathBuf::from("/pkg");
            open_path.extend(path_iter);
            io_util::connect_in_namespace(
                open_path.to_str().unwrap(),
                server_end.into_channel(),
                flags,
            )
            .expect("failed to open path in namespace");
        } else {
            // Create a new fake connection to handle this open, but using the 2nd mode.
            Self::new_internal(true, ServerEnd::new(server_end.into_channel())).unwrap();
        }
    }
}
