// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fidl::{
        endpoints::{Proxy, ServerEnd},
        HandleBased,
    },
    fidl_fuchsia_device::{ControllerMarker, ControllerProxy},
    fidl_fuchsia_hardware_block_encrypted::{DeviceManagerMarker, DeviceManagerProxy},
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::VolumeManagerMarker,
    fidl_fuchsia_identity_account::{
        AccountManagerMarker, AccountManagerProxy, AccountMetadata, AccountProxy,
    },
    fidl_fuchsia_io as fio,
    fuchsia_async::{
        self as fasync,
        futures::{FutureExt as _, StreamExt as _},
        DurationExt as _, TimeoutExt as _,
    },
    fuchsia_component_test::new::{
        Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
    },
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    fuchsia_zircon::{self as zx, sys::zx_status_t, Status},
    ramdevice_client::{RamdiskClient, RamdiskClientBuilder},
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::{fs, os::raw::c_int, time::Duration},
    storage_isolated_driver_manager::bind_fvm,
};

// Canonically defined in //zircon/system/public/zircon/hw/gpt.h
const FUCHSIA_DATA_GUID_VALUE: [u8; 16] = [
    // 08185F0C-892D-428A-A789-DBEEC8F55E6A
    0x0c, 0x5f, 0x18, 0x08, 0x2d, 0x89, 0x8a, 0x42, 0xa7, 0x89, 0xdb, 0xee, 0xc8, 0xf5, 0x5e, 0x6a,
];
const FUCHSIA_DATA_GUID: Guid = Guid { value: FUCHSIA_DATA_GUID_VALUE };
const ACCOUNT_LABEL: &str = "account";
const RAMCTL_PATH: &'static str = "sys/platform/00:00:2d/ramctl";
const BLOCK_SIZE: u64 = 4096;
const BLOCK_COUNT: u64 = 1024; // 4MB RAM ought to be good enough

// 1 block for zxcrypt, and minfs needs at least 3 blocks.
const FVM_SLICE_SIZE: usize = BLOCK_SIZE as usize * 4;

// The maximum time to wait for a `wait_for_device_at` call. For whatever reason, using
// `Duration::MAX` seems to trigger immediate ZX_ERR_TIMED_OUT in the wait_for_device_at calls, so
// we just set a quite large timeout here.
const DEVICE_WAIT_TIMEOUT: Duration = Duration::from_secs(60);

// The maximum time to wait for an account channel to close after the account is locked.
const ACCOUNT_CLOSE_TIMEOUT: zx::Duration = zx::Duration::from_seconds(5);

const GLOBAL_ACCOUNT_ID: u64 = 1;
const EMPTY_PASSWORD: &'static str = "";
const REAL_PASSWORD: &'static str = "a real passphrase!";

#[link(name = "fs-management")]
extern "C" {
    pub fn fvm_init(fd: c_int, slice_size: usize) -> zx_status_t;
}

struct TestEnv {
    realm_instance: RealmInstance,
}

impl TestEnv {
    async fn build() -> TestEnv {
        let builder = RealmBuilder::new().await.unwrap();
        builder.driver_test_realm_setup().await.unwrap();
        let password_authenticator = builder
            .add_child("password_authenticator", "fuchsia-pkg://fuchsia.com/password-authenticator-integration-tests#meta/password-authenticator.cm", ChildOptions::new()).await.unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .capability(Capability::protocol_by_name("fuchsia.process.Launcher"))
                    .capability(Capability::storage("data"))
                    .from(Ref::parent())
                    .to(&password_authenticator),
            )
            .await
            .unwrap();

        // Expose AccountManager so we can test it
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<AccountManagerMarker>())
                    .from(&password_authenticator)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        // Offer /dev from DriverTestrealm to password_authenticator, which makes use of it.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("dev"))
                    .from(Ref::child("driver_test_realm"))
                    .to(&password_authenticator),
            )
            .await
            .unwrap();

        let realm_instance = builder.build().await.unwrap();
        let args = fidl_fuchsia_driver_test::RealmArgs {
            root_driver: Some("fuchsia-boot:///#driver/platform-bus.so".to_string()),
            ..fidl_fuchsia_driver_test::RealmArgs::EMPTY
        };
        realm_instance.driver_test_realm_start(args).await.unwrap();

        TestEnv { realm_instance }
    }

    pub async fn setup_ramdisk(&self, mut type_guid: Guid, name: &str) -> RamdiskClient {
        let dev_root_fd = self.dev_root_fd();

        // Wait for ramctl in namespace at /dev/sys/platform/00:00:2d/ramctl
        ramdevice_client::wait_for_device_at(&dev_root_fd, RAMCTL_PATH, DEVICE_WAIT_TIMEOUT)
            .expect("Could not wait for ramctl from isolated-devmgr");

        // Create ramdisk
        let ramdisk = RamdiskClientBuilder::new(BLOCK_SIZE, BLOCK_COUNT)
            .dev_root(self.dev_root_fd())
            .build()
            .expect("Could not create ramdisk");

        // Open ramdisk device and initialize FVM
        {
            let ramdisk_handle = ramdisk.open().expect("Could not re-open ramdisk").into_handle();
            let ramdisk_fd = fdio::create_fd(ramdisk_handle).expect("create fd of dev root");
            let status = unsafe { fvm_init(ramdisk_fd, FVM_SLICE_SIZE) };
            Status::ok(status).expect("could not initialize FVM structures in ramdisk");
            // ramdisk_file drops, closing the fd we created
        }

        // Open ramdisk device again as fidl_fuchsia_device::ControllerProxy
        let ramdisk_chan = ramdisk.open().expect("Could not re-open ramdisk");
        let controller_chan = fasync::Channel::from_channel(ramdisk_chan)
            .expect("Could not convert ramdisk channel to async channel");
        let controller = ControllerProxy::from_channel(controller_chan);

        // Bind FVM to that ramdisk
        bind_fvm(&controller).await.expect("Could not bind FVM");

        // wait for /fvm child device to appear and open it
        let fvm_path = ramdisk.get_path().to_string() + "/fvm";
        ramdevice_client::wait_for_device_at(&dev_root_fd, &fvm_path, DEVICE_WAIT_TIMEOUT)
            .expect("Could not wait for fvm from isolated-devmgr");

        let (volume_manager_client, volume_manager_server) =
            fidl::endpoints::create_proxy::<VolumeManagerMarker>()
                .expect("Could not create volume manager channel pair");
        self.dev_root()
            .open(
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_SERVICE,
                &fvm_path,
                ServerEnd::new(volume_manager_server.into_channel()),
            )
            .expect("Could not connect to VolumeManager");

        // create FVM child volume with desired GUID/label
        let mut rng = SmallRng::from_entropy();
        let mut instance_guid = Guid { value: rng.gen() };
        let status = volume_manager_client
            .allocate_partition(1, &mut type_guid, &mut instance_guid, name, 0)
            .await
            .expect("Could not request to create volume");
        Status::ok(status).expect("Could not create volume");

        let fvm_inner_block_path = fvm_path + "/" + name + "-p-1/block";
        ramdevice_client::wait_for_device_at(
            &dev_root_fd,
            &fvm_inner_block_path,
            DEVICE_WAIT_TIMEOUT,
        )
        .expect("Could not wait for inner fvm block device");

        // Return handle to ramdisk since RamdiskClient's Drop impl destroys the ramdisk.
        ramdisk
    }

    pub fn open_zxcrypt_manager(&self, ramdisk: &RamdiskClient, name: &str) -> DeviceManagerProxy {
        let (manager_client, manager_server) =
            fidl::endpoints::create_proxy::<DeviceManagerMarker>()
                .expect("Could not create encryption volume manager channel pair");
        let mgr_path = ramdisk.get_path().to_string() + "/fvm/" + name + "-p-1/block/zxcrypt";
        self.dev_root()
            .open(
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_SERVICE,
                &mgr_path,
                ServerEnd::new(manager_server.into_channel()),
            )
            .expect("Could not connect to zxcrypt manager");

        manager_client
    }

    pub async fn format_zxcrypt(&self, ramdisk: &RamdiskClient, name: &str) {
        let (controller_client, controller_server) =
            fidl::endpoints::create_proxy::<ControllerMarker>().expect("create channel pair");
        let block_path = ramdisk.get_path().to_string() + "/fvm/" + name + "-p-1/block";
        self.dev_root()
            .open(
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_SERVICE,
                &block_path,
                ServerEnd::new(controller_server.into_channel()),
            )
            .expect("Could not connect to fvm block device");

        // Bind the zxcrypt driver to the block device
        controller_client
            .bind("zxcrypt.so")
            .await
            .expect("Could not send request to bind zxcrypt driver")
            .expect("Could not bind zxcrypt driver");

        // Wait for zxcrypt device manager node to appear
        let zxcrypt_path = block_path + "/zxcrypt";
        let dev_root_fd = self.dev_root_fd();
        ramdevice_client::wait_for_device_at(&dev_root_fd, &zxcrypt_path, DEVICE_WAIT_TIMEOUT)
            .expect("wait for zxcrypt from isolated-devmgr");

        // Open zxcrypt device manager node
        let manager = self.open_zxcrypt_manager(ramdisk, name);
        let key: [u8; 32] = [0; 32];
        manager.format(&key, 0).await.expect("Could not format zxcrypt");
    }

    pub fn dev_root(&self) -> fio::DirectoryProxy {
        let (dev_dir_client, dev_dir_server) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().expect("create channel pair");

        self.realm_instance
            .root
            .get_exposed_dir()
            .open(
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                "dev",
                ServerEnd::new(dev_dir_server.into_channel()),
            )
            .expect("Get /dev from isolated_devmgr");
        dev_dir_client
    }

    pub fn dev_root_fd(&self) -> fs::File {
        let dev_root_proxy = self.dev_root();
        fdio::create_fd(
            dev_root_proxy
                .into_channel()
                .expect("Could not convert dev root DirectoryProxy into channel")
                .into_zx_channel()
                .into_handle(),
        )
        .expect("create fd of dev root")
    }

    pub fn account_manager(&self) -> AccountManagerProxy {
        self.realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<AccountManagerMarker>()
            .expect("connect to account manager")
    }
}

/// Waits up to ACCOUNT_CLOSE_TIMEOUT for the supplied account to close.
async fn wait_for_account_close(account: &AccountProxy) -> Result<(), Error> {
    account
        .take_event_stream()
        .for_each(|_| async move {}) // Drain all remaining events
        .map(|_| Ok(())) // Completing the drain results in ok
        .on_timeout(ACCOUNT_CLOSE_TIMEOUT.after_now(), || {
            Err(anyhow!("Account close timeout exceeded"))
        })
        .await
}

#[fuchsia::test]
async fn get_account_ids_unprovisioned() {
    let env = TestEnv::build().await;
    let account_ids = env.account_manager().get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![]);
}

#[fuchsia::test]
async fn deprecated_provision_new_null_password_account_on_unformatted_partition() {
    let env = TestEnv::build().await;
    let _ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![]);

    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            EMPTY_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);
}

#[fuchsia::test]
async fn deprecated_provision_new_real_password_account_on_unformatted_partition() {
    let env = TestEnv::build().await;
    let _ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![]);

    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            REAL_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);
}

#[fuchsia::test]
async fn deprecated_provision_new_real_password_account_on_formatted_partition() {
    // We expect account_manager to ignore the data in the zxcrypt volume, because the account
    // metadata store is the canonical "does this account exist" indicator, and it has no existing
    // accounts.

    let env = TestEnv::build().await;
    let ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    env.format_zxcrypt(&ramdisk, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    // Provision the account.
    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            REAL_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);
}

#[fuchsia::test]
async fn deprecated_provision_new_account_over_existing_account_fails() {
    let env = TestEnv::build().await;
    let ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    env.format_zxcrypt(&ramdisk, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, Vec::<u64>::new());

    // Provision the account.
    let (account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            REAL_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);
    account_proxy.lock().await.expect("lock FIDL").expect("locked");
    drop(account_proxy);

    // A second attempt to provision the same user over the existing account should fail, since
    // the account for the global account ID has already been provisioned.
    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let error = account_manager
        .deprecated_provision_new_account(
            REAL_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect_err("deprecated provision new account should fail");
    assert_eq!(error, fidl_fuchsia_identity_account::Error::FailedPrecondition);
}

#[fuchsia::test]
async fn deprecated_provision_new_account_formats_directory() {
    let env = TestEnv::build().await;
    let _ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![]);

    let expected_content = b"some data";
    {
        let (account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .deprecated_provision_new_account(
                REAL_PASSWORD,
                AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
                server_end,
            )
            .await
            .expect("deprecated_new_provision FIDL")
            .expect("deprecated provision new account");

        let (root, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_proxy
            .get_data_directory(server_end)
            .await
            .expect("get_data_directory FIDL")
            .expect("get_data_directory");
        let file = io_util::directory::open_file(
            &root,
            "test",
            fio::OPEN_FLAG_CREATE | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        )
        .await
        .expect("create file");

        let bytes_written = file
            .write(expected_content)
            .await
            .expect("file write")
            .map_err(Status::from_raw)
            .expect("failed to write content");
        assert_eq!(bytes_written, expected_content.len() as u64);
    }

    let (account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_get_account(GLOBAL_ACCOUNT_ID, REAL_PASSWORD, server_end)
        .await
        .expect("deprecated_get_account FIDL")
        .expect("deprecated_get_account");

    let (root, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_proxy
        .get_data_directory(server_end)
        .await
        .expect("get_data_directory FIDL")
        .expect("get_data_directory");
    let file = io_util::directory::open_file(&root, "test", fio::OPEN_RIGHT_READABLE)
        .await
        .expect("create file");

    let actual_contents = io_util::file::read(&file).await.expect("read file");
    assert_eq!(&actual_contents, expected_content);
}

#[fuchsia::test]
async fn locked_account_can_be_unlocked_again() {
    let env = TestEnv::build().await;
    let _ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let expected_content = b"some data";

    let (account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            REAL_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");
    let root = {
        let (root, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_proxy
            .get_data_directory(server_end)
            .await
            .expect("get_data_directory FIDL")
            .expect("get_data_directory");

        // Write a file to the data directory.
        let file = io_util::directory::open_file(
            &root,
            "test",
            fio::OPEN_FLAG_CREATE | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        )
        .await
        .expect("create file");

        let bytes_written = file
            .write(expected_content)
            .await
            .expect("file write")
            .map_err(Status::from_raw)
            .expect("failed to write content");
        assert_eq!(bytes_written, expected_content.len() as u64);
        root
    };

    // Lock the account.
    account_proxy.lock().await.expect("lock FIDL").expect("locked");

    // The data directory should be closed.
    io_util::directory::open_file(&root, "test", fio::OPEN_RIGHT_READABLE)
        .await
        .expect_err("failed to open file");

    // Attempt to call get_data_directory. Its very likely the account channel will have been closed
    // before we can make this request, but if the request is accepted the response should indicate
    // a failed precondition now that the account is locked.
    let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
    match account_proxy.get_data_directory(server_end).await {
        Err(_) => (), // FIDL error means the channel was already closed
        Ok(gdd_result) => {
            gdd_result.expect_err("get_data_directory succeeded after lock");
            // Verify the account channel does actually close shortly after.
            wait_for_account_close(&account_proxy).await.unwrap();
        }
    }

    // Unlock the account again.
    let (account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_get_account(GLOBAL_ACCOUNT_ID, REAL_PASSWORD, server_end)
        .await
        .expect("deprecated_get_account FIDL")
        .expect("deprecated_get_account");

    // Look for the file written previously.
    let (root, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_proxy
        .get_data_directory(server_end)
        .await
        .expect("get_data_directory FIDL")
        .expect("get_data_directory");
    let file = io_util::directory::open_file(&root, "test", fio::OPEN_RIGHT_READABLE)
        .await
        .expect("create file");

    let actual_contents = io_util::file::read(&file).await.expect("read file");
    assert_eq!(&actual_contents, expected_content);
}

#[fuchsia::test]
async fn locking_account_terminates_all_clients() {
    let env = TestEnv::build().await;
    let _ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let (account_proxy1, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            REAL_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");

    let (account_proxy2, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_get_account(GLOBAL_ACCOUNT_ID, REAL_PASSWORD, server_end)
        .await
        .expect("deprecated_get_account FIDL")
        .expect("deprecated_get_account");

    // Calling lock on one account channel should close both.
    account_proxy1.lock().await.expect("lock FIDL").expect("lock");

    // Verify that both account channels are closed.
    futures::try_join!(
        wait_for_account_close(&account_proxy1),
        wait_for_account_close(&account_proxy2),
    )
    .expect("waiting for account channels to close");
}

#[fuchsia::test]
async fn null_provision_and_unlock_with_real_password_fails_with_failed_authentication() {
    let env = TestEnv::build().await;
    let _ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![]);

    let (account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            EMPTY_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);

    account_proxy.lock().await.expect("lock FIDL").expect("locked");
    drop(account_proxy);

    // A second attempt to provision the same user over the existing account should fail, since
    // the account for the global account ID has already been provisioned.
    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let error = account_manager
        .deprecated_get_account(account_ids[0], REAL_PASSWORD, server_end)
        .await
        .expect("deprecated_new_provision FIDL")
        .expect_err("deprecated provision new account should fail");
    assert_eq!(error, fidl_fuchsia_identity_account::Error::FailedAuthentication);
}

#[fuchsia::test]
async fn remove_account_succeeds_and_terminates_clients() {
    let env = TestEnv::build().await;
    let _ramdisk = env.setup_ramdisk(FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let (account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            REAL_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);

    account_manager
        .remove_account(account_ids[0], false)
        .await
        .expect("remove_account FIDL")
        .expect("remove_account");

    wait_for_account_close(&account_proxy).await.expect("remove_account closes channel");

    let account_ids_after =
        account_manager.get_account_ids().await.expect("get account ids after remove");
    assert_eq!(account_ids_after, Vec::<u64>::new());

    // After removal, the account can be provisioned again.
    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_provision_new_account(
            REAL_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect("deprecated provision new account");

    let account_ids =
        account_manager.get_account_ids().await.expect("get account ids after second provision");
    assert_eq!(account_ids, vec![1]);
}
