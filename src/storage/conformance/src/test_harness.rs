// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::flags::build_flag_combinations, fidl::endpoints::create_proxy, fidl_fuchsia_io as io,
    fidl_fuchsia_io_test as io_test, fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
};

/// Helper struct for connecting to an io1 test harness and running a conformance test on it.
pub struct TestHarness {
    /// FIDL proxy to the io1 test harness.
    pub proxy: io_test::Io1HarnessProxy,

    /// Config for the filesystem.
    pub config: io_test::Io1Config,

    /// All rights supported by the filesystem.
    pub all_rights: u32,
}

impl TestHarness {
    /// Connects to the test harness and returns a `TestHarness` struct.
    pub async fn new() -> TestHarness {
        let proxy = connect_to_harness().await;
        let config = proxy.get_config().await.expect("Could not get config from proxy");
        let all_rights = get_supported_rights(&config);

        TestHarness { proxy, config, all_rights }
    }

    /// Creates and returns a directory with the given structure from the test harness.
    pub fn get_directory(&self, root: io_test::Directory, flags: u32) -> io::DirectoryProxy {
        let (client, server) = create_proxy::<io::DirectoryMarker>().expect("Cannot create proxy");
        self.proxy
            .get_directory(root, flags, server)
            .expect("Cannot get directory from test harness");
        client
    }

    /// Returns all combinations of supported flags.
    pub fn all_flag_combos(&self) -> Vec<u32> {
        build_flag_combinations(0, self.all_rights)
    }

    /// Returns all combinations of supported flags that include `OPEN_RIGHT_READABLE`.
    pub fn readable_flag_combos(&self) -> Vec<u32> {
        build_flag_combinations(io::OPEN_RIGHT_READABLE, self.all_rights)
    }

    /// Returns all combinations of supported flags that include `OPEN_RIGHT_WRITABLE`.
    pub fn writable_flag_combos(&self) -> Vec<u32> {
        build_flag_combinations(io::OPEN_RIGHT_WRITABLE, self.all_rights)
    }

    /// Returns all combinations of supported flags that do not include `OPEN_RIGHT_READABLE`.
    pub fn non_readable_flag_combos(&self) -> Vec<u32> {
        let non_readable_rights = self.all_rights & !io::OPEN_RIGHT_READABLE;
        build_flag_combinations(0, non_readable_rights)
    }

    /// Returns all combinations of supported flags that do not include `OPEN_RIGHT_WRITABLE`.
    pub fn non_writable_flag_combos(&self) -> Vec<u32> {
        let non_writable_rights = self.all_rights & !io::OPEN_RIGHT_WRITABLE;
        build_flag_combinations(0, non_writable_rights)
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

/// Returns a constant representing the aggregate of all io.fidl supported_rights that are supported by the
/// test harness.
fn get_supported_rights(config: &io_test::Io1Config) -> u32 {
    let mut rights = io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE;

    if !config.no_exec.unwrap_or_default() {
        rights |= io::OPEN_RIGHT_EXECUTABLE;
    }
    if !config.no_admin.unwrap_or_default() {
        rights |= io::OPEN_RIGHT_ADMIN;
    }
    rights
}
