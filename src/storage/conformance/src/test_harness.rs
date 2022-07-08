// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::flags::Rights, fidl::endpoints::create_proxy, fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io as fio, fidl_fuchsia_io_test as io_test,
    fuchsia_zircon as zx,
};

/// Helper struct for connecting to an io1 test harness and running a conformance test on it.
pub struct TestHarness {
    /// FIDL proxy to the io1 test harness.
    pub proxy: io_test::Io1HarnessProxy,

    /// Config for the filesystem.
    pub config: io_test::Io1Config,

    /// All [`io_test::Directory`] rights supported by the filesystem.
    pub dir_rights: Rights,

    /// All [`io_test::File`] rights supported by the filesystem.
    pub file_rights: Rights,

    /// All [`io_test::VmoFile`] rights supported by the filesystem.
    pub vmo_file_rights: Rights,

    /// All [`io_test::ExecutableFile`] rights supported by the filesystem.
    pub executable_file_rights: Rights,
}

impl TestHarness {
    /// Connects to the test harness and returns a `TestHarness` struct.
    pub async fn new() -> TestHarness {
        let proxy = connect_to_harness().await;
        let config = proxy.get_config().await.expect("Could not get config from proxy");

        // Validate configuration options for consistency, disallow invalid combinations.
        if config.supports_executable_file.unwrap_or_default() {
            assert!(
                config.supports_get_backing_memory.unwrap_or_default(),
                "GetBackingMemory must be supported for testing ExecutableFile objects!"
            );
        }
        if config.supports_rename.unwrap_or_default() || config.supports_link.unwrap_or_default() {
            assert!(
                config.supports_get_token.unwrap_or_default(),
                "GetToken must be supported for testing Rename/Link!"
            );
        }

        // Generate set of supported open rights for each object type.
        let dir_rights = Rights::new(get_supported_dir_rights(&config));
        let file_rights = Rights::new(get_supported_file_rights(&config));
        let vmo_file_rights = Rights::new(get_supported_vmo_file_rights());
        let executable_file_rights = Rights::new(get_supported_executable_file_rights());

        TestHarness {
            proxy,
            config,
            dir_rights,
            file_rights,
            vmo_file_rights,
            executable_file_rights,
        }
    }

    /// Creates a [`fio::DirectoryProxy`] with the given root directory structure.
    pub fn get_directory(
        &self,
        root: io_test::Directory,
        flags: fio::OpenFlags,
    ) -> fio::DirectoryProxy {
        let (client, server) = create_proxy::<fio::DirectoryMarker>().expect("Cannot create proxy");
        self.proxy
            .get_directory(root, flags, server)
            .expect("Cannot get directory from test harness");
        client
    }
}

async fn connect_to_harness() -> io_test::Io1HarnessProxy {
    // Connect to the realm to get acccess to the outgoing directory for the harness.
    let (client, server) = zx::Channel::create().expect("Cannot create channel");
    fuchsia_component::client::connect_channel_to_protocol::<fcomponent::RealmMarker>(server)
        .expect("Cannot connect to Realm service");
    let realm = fcomponent::RealmSynchronousProxy::new(client);
    // fs_test is the name of the child component defined in the manifest.
    let mut child_ref = fdecl::ChildRef { name: "fs_test".to_string(), collection: None };
    let (client, server) = zx::Channel::create().expect("Cannot create channel");
    realm
        .open_exposed_dir(
            &mut child_ref,
            fidl::endpoints::ServerEnd::<fio::DirectoryMarker>::new(server),
            zx::Time::INFINITE,
        )
        .expect("FIDL error when binding to child in Realm")
        .expect("Cannot bind to test harness child in Realm");

    let exposed_dir = fio::DirectoryProxy::new(
        fidl::AsyncChannel::from_channel(client).expect("Cannot create async channel"),
    );

    fuchsia_component::client::connect_to_protocol_at_dir_root::<io_test::Io1HarnessMarker>(
        &exposed_dir,
    )
    .expect("Cannot connect to test harness protocol")
}

/// Returns the aggregate of all rights that are supported for [`io_test::Directory`] objects.
///
/// Must support read, write, execute.
fn get_supported_dir_rights(_config: &io_test::Io1Config) -> fio::OpenFlags {
    fio::OpenFlags::RIGHT_READABLE
        | fio::OpenFlags::RIGHT_WRITABLE
        | fio::OpenFlags::RIGHT_EXECUTABLE
}

/// Returns the aggregate of all rights that are supported for [`io_test::File`] objects.
///
/// Must support read, and optionally, write (if mutable_file is true).
fn get_supported_file_rights(config: &io_test::Io1Config) -> fio::OpenFlags {
    let mut rights = fio::OpenFlags::RIGHT_READABLE;
    if config.mutable_file.unwrap_or_default() {
        rights |= fio::OpenFlags::RIGHT_WRITABLE;
    }
    rights
}

/// Returns the aggregate of all rights that are supported for [`io_test::VmoFile`] objects.
///
/// Must support both read and write.
fn get_supported_vmo_file_rights() -> fio::OpenFlags {
    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE
}

/// Returns the aggregate of all rights that are supported for [`io_test::ExecutableFile`] objects.
///
/// Must support both read and execute.
fn get_supported_executable_file_rights() -> fio::OpenFlags {
    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE
}
