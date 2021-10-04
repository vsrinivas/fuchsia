// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::flags::Rights,
    fidl::endpoints::{create_proxy, ClientEnd},
    fidl_fuchsia_io as io, fidl_fuchsia_io_test as io_test, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon as zx,
};

/// Helper struct for connecting to an io1 test harness and running a conformance test on it.
pub struct TestHarness {
    /// FIDL proxy to the io1 test harness.
    pub proxy: io_test::Io1HarnessProxy,

    /// Config for the filesystem.
    pub config: io_test::Io1Config,

    /// All Directory rights supported by the filesystem.
    pub dir_rights: Rights,

    /// All File rights supported by the filesystem.
    pub file_rights: Rights,

    /// All VmoFile rights supported by the filesystem.
    pub vmofile_rights: Rights,

    /// All ExecFile rights supported by the filesystem.
    pub execfile_rights: Rights,
}

impl TestHarness {
    /// Connects to the test harness and returns a `TestHarness` struct.
    pub async fn new() -> TestHarness {
        let proxy = connect_to_harness().await;
        let config = proxy.get_config().await.expect("Could not get config from proxy");
        let dir_rights = Rights::new(get_supported_dir_rights(&config));
        let file_rights = Rights::new(get_supported_file_rights(&config));
        let vmofile_rights = Rights::new(get_supported_vmofile_rights());
        let execfile_rights = Rights::new(get_supported_execfile_rights());

        // TODO(fxbug.dev/77633): Validate configuration options for consistency, e.g.:
        //  - If no_get_buffer is false, no_vmofile should be false
        //  - If no_execfile is false, no_get_buffer should be false
        //  - If no_admin or no_link is false, immutable_dir should be false (what about no_rename?)

        TestHarness { proxy, config, dir_rights, file_rights, vmofile_rights, execfile_rights }
    }

    /// Creates a DirectoryProxy with the given root directory structure.
    pub fn get_directory(&self, root: io_test::Directory, flags: u32) -> io::DirectoryProxy {
        let (client, server) = create_proxy::<io::DirectoryMarker>().expect("Cannot create proxy");
        self.proxy
            .get_directory(root, flags, server)
            .expect("Cannot get directory from test harness");
        client
    }

    /// Creates a DirectoryProxy with the specified remote directory mounted at the given path.
    pub fn get_directory_with_remote_directory(
        &self,
        remote_dir: ClientEnd<io::DirectoryMarker>,
        path: &str,
        flags: u32,
    ) -> io::DirectoryProxy {
        let (client, server) = create_proxy::<io::DirectoryMarker>().expect("Cannot create proxy");
        self.proxy
            .get_directory_with_remote_directory(remote_dir, path, flags, server)
            .expect("Cannot get remote directory from test harness");
        client
    }
}

async fn connect_to_harness() -> io_test::Io1HarnessProxy {
    // Connect to the realm to get acccess to the outgoing directory for the harness.
    let (client, server) = zx::Channel::create().expect("Cannot create channel");
    fuchsia_component::client::connect_channel_to_protocol::<fsys::RealmMarker>(server)
        .expect("Cannot connect to Realm service");
    let realm = fsys::RealmSynchronousProxy::new(client);
    // fs_test is the name of the child component defined in the manifest.
    let mut child_ref = fsys::ChildRef { name: "fs_test".to_string(), collection: None };
    let (client, server) = zx::Channel::create().expect("Cannot create channel");
    realm
        .open_exposed_dir(
            &mut child_ref,
            fidl::endpoints::ServerEnd::<io::DirectoryMarker>::new(server),
            zx::Time::INFINITE,
        )
        .expect("FIDL error when binding to child in Realm")
        .expect("Cannot bind to test harness child in Realm");

    let exposed_dir = io::DirectoryProxy::new(
        fidl::AsyncChannel::from_channel(client).expect("Cannot create async channel"),
    );

    fuchsia_component::client::connect_to_protocol_at_dir_root::<io_test::Io1HarnessMarker>(
        &exposed_dir,
    )
    .expect("Cannot connect to test harness protocol")
}

/// Returns the aggregate of all rights that are supported for Directory objects.
///
/// Must support read, write, execute, and optionally, admin (if no_admin == false).
fn get_supported_dir_rights(config: &io_test::Io1Config) -> u32 {
    let mut rights = io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE | io::OPEN_RIGHT_EXECUTABLE;
    if !config.no_admin.unwrap_or_default() {
        rights |= io::OPEN_RIGHT_ADMIN;
    }
    rights
}

/// Returns the aggregate of all rights that are supported for File objects.
///
/// Must support read, and optionally, write (if immutable_file == true).
fn get_supported_file_rights(config: &io_test::Io1Config) -> u32 {
    let mut rights = io::OPEN_RIGHT_READABLE;
    if !config.immutable_file.unwrap_or_default() {
        rights |= io::OPEN_RIGHT_WRITABLE;
    }
    rights
}

/// Returns the aggregate of all rights that are supported for VmoFile objects.
///
/// Must support both read and write.
fn get_supported_vmofile_rights() -> u32 {
    io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE
}

/// Returns the aggregate of all rights that are supported for ExecFile objects.
///
/// Must support both read and execute.
fn get_supported_execfile_rights() -> u32 {
    io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_EXECUTABLE
}
