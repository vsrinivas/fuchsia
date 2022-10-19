// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_identity_account::AccountMetadata,
    fidl_fuchsia_io as fio,
    fuchsia_zircon::Status,
    ramdisk_common::{format_zxcrypt, setup_ramdisk},
};

use crate::{
    wait_for_account_close, Config, TestEnv, ACCOUNT_LABEL, EMPTY_PASSWORD, FUCHSIA_DATA_GUID,
    GLOBAL_ACCOUNT_ID, REAL_PASSWORD,
};

#[fuchsia::test]
async fn get_account_ids_unprovisioned() {
    let env = TestEnv::build(Config::ScryptOnly).await;
    let account_ids = env.account_manager().get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, Vec::<u64>::new());
}

#[fuchsia::test]
async fn deprecated_provision_new_null_password_account_while_null_disallowed() {
    let env = TestEnv::build(Config::ScryptOnly).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, Vec::<u64>::new());

    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let error = account_manager
        .deprecated_provision_new_account(
            EMPTY_PASSWORD,
            AccountMetadata { name: Some("test".to_string()), ..AccountMetadata::EMPTY },
            server_end,
        )
        .await
        .expect("deprecated_new_provision FIDL")
        .expect_err("deprecated provision new account should fail");
    assert_eq!(error, fidl_fuchsia_identity_account::Error::InvalidRequest);

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, Vec::<u64>::new());
}

#[fuchsia::test]
async fn deprecated_provision_new_real_password_account_on_unformatted_partition() {
    let env = TestEnv::build(Config::ScryptOnly).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, Vec::<u64>::new());

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

    let env = TestEnv::build(Config::ScryptOnly).await;
    let ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    format_zxcrypt(&env.realm_instance, &ramdisk, ACCOUNT_LABEL).await;
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
    let env = TestEnv::build(Config::ScryptOnly).await;
    let ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    format_zxcrypt(&env.realm_instance, &ramdisk, ACCOUNT_LABEL).await;
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
    let env = TestEnv::build(Config::ScryptOnly).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, Vec::<u64>::new());

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
        let file = fuchsia_fs::directory::open_file(
            &root,
            "test",
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
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
    let file = fuchsia_fs::directory::open_file(&root, "test", fio::OpenFlags::RIGHT_READABLE)
        .await
        .expect("create file");

    let actual_contents = fuchsia_fs::file::read(&file).await.expect("read file");
    assert_eq!(&actual_contents, expected_content);
}

#[fuchsia::test]
async fn locked_account_can_be_unlocked_again() {
    let env = TestEnv::build(Config::ScryptOnly).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
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
        let file = fuchsia_fs::directory::open_file(
            &root,
            "test",
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
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
    fuchsia_fs::directory::open_file(&root, "test", fio::OpenFlags::RIGHT_READABLE)
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
    let file = fuchsia_fs::directory::open_file(&root, "test", fio::OpenFlags::RIGHT_READABLE)
        .await
        .expect("create file");

    let actual_contents = fuchsia_fs::file::read(&file).await.expect("read file");
    assert_eq!(&actual_contents, expected_content);
}

#[fuchsia::test]
async fn locking_account_terminates_all_clients() {
    let env = TestEnv::build(Config::ScryptOnly).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
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
async fn remove_account_succeeds_and_terminates_clients() {
    let env = TestEnv::build(Config::ScryptOnly).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
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
        .remove_account(account_ids[0])
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
