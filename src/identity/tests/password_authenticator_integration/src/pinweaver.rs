// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_identity_account::{AccountManagerProxy, AccountMetadata, AccountProxy},
    fidl_fuchsia_io as fio,
    fuchsia_zircon::Status,
    ramdisk_common::setup_ramdisk,
};

use {
    crate::mock_cr50_agent::MockCr50AgentBuilder,
    crate::{
        wait_for_account_close, Config, TestEnv, ACCOUNT_LABEL, BAD_PASSWORD, FUCHSIA_DATA_GUID,
        GLOBAL_ACCOUNT_ID, REAL_PASSWORD,
    },
};

async fn test_pinweaver_provision_account(account_manager: &AccountManagerProxy) -> AccountProxy {
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
    account_proxy
}

async fn test_pinweaver_successful_auth(account_manager: &AccountManagerProxy) -> AccountProxy {
    let (account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_get_account(GLOBAL_ACCOUNT_ID, REAL_PASSWORD, server_end)
        .await
        .expect("deprecated_get_account FIDL")
        .expect("deprecated_get_account");
    account_proxy
}

async fn test_pinweaver_failed_auth(account_manager: &AccountManagerProxy) {
    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_get_account(GLOBAL_ACCOUNT_ID, BAD_PASSWORD, server_end)
        .await
        .expect("deprecated_get_account FIDL")
        .expect_err("deprecated_get_account");
}

async fn test_pinweaver_rate_limited_auth(account_manager: &AccountManagerProxy) {
    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    // RateLimited is indistinguishable from Failed auth at the AccountManager proxy level.
    account_manager
        .deprecated_get_account(GLOBAL_ACCOUNT_ID, REAL_PASSWORD, server_end)
        .await
        .expect("deprecated_get_account FIDL")
        .expect_err("deprecated_get_account");
}

#[fuchsia::test]
async fn test_pinweaver_locked_account_can_be_unlocked_again() {
    let mocks = MockCr50AgentBuilder::new()
        .add_reset_tree_response([0; 32])
        // During account provisioning PwAuth also makes an InsertLeaf and a TryAuth call.
        .add_insert_leaf_response([0; 32], [1; 32], vec![0, 1, 2, 3])
        .add_try_auth_success_response([0; 32], vec![0, 1, 2, 3], [1; 32])
        // Pass authentication to the account, assuming a good passphrase.
        .add_try_auth_success_response([0; 32], vec![0, 1, 2, 3], [1; 32])
        .build();
    let env = TestEnv::build_with_cr50_mock(Config::PinweaverOrScrypt, Some(mocks)).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    let expected_content = b"some data";

    // Provision a new account and write a file to it.
    let account_proxy = test_pinweaver_provision_account(&account_manager).await;
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
    let account_proxy = test_pinweaver_successful_auth(&account_manager).await;

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
async fn test_pinweaver_bad_password_cannot_unlock_account() {
    let mocks = MockCr50AgentBuilder::new()
        .add_reset_tree_response([0; 32])
        // During account provisioning PwAuth also makes an InsertLeaf and a TryAuth call.
        .add_insert_leaf_response([0; 32], [1; 32], vec![0, 1, 2, 3])
        .add_try_auth_success_response([0; 32], vec![0, 1, 2, 3], [1; 32])
        // Fail authentication to the account, assuming a bad passphrase.
        .add_try_auth_failed_response([0; 32], vec![0, 1, 2, 3], [1; 32])
        .build();
    let env = TestEnv::build_with_cr50_mock(Config::PinweaverOrScrypt, Some(mocks)).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    // Provision the account.
    let account_proxy = test_pinweaver_provision_account(&account_manager).await;
    let (root, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_proxy
        .get_data_directory(server_end)
        .await
        .expect("get_data_directory FIDL")
        .expect("get_data_directory");

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

    // Fail to unlock the account again with the wrong password.
    test_pinweaver_failed_auth(&account_manager).await;
}

#[fuchsia::test]
async fn test_pinweaver_provision_and_remove_account_can_provision_again() {
    // Set up the mock cr50 responses.
    let mocks = MockCr50AgentBuilder::new()
        .add_reset_tree_response([0; 32])
        // During account provisioning PwAuth also makes an InsertLeaf and a TryAuth call.
        .add_insert_leaf_response([0; 32], [1; 32], vec![0, 1, 2, 3])
        .add_try_auth_success_response([0; 32], vec![0, 1, 2, 3], [1; 32])
        // Remove the provisioned account.
        .add_remove_leaf_response([0; 32])
        // Provision another account.
        .add_insert_leaf_response([0; 32], [2; 32], vec![0, 1, 2, 3])
        .add_try_auth_success_response([0; 32], vec![4, 5, 6, 7], [2; 32])
        .build();
    let env = TestEnv::build_with_cr50_mock(Config::PinweaverOrScrypt, Some(mocks)).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    // Provision an account.
    let account_proxy = test_pinweaver_provision_account(&account_manager).await;

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);

    // Remove the account.
    account_manager
        .remove_account(account_ids[0])
        .await
        .expect("remove_account FIDL")
        .expect("remove_account");

    wait_for_account_close(&account_proxy).await.expect("remove_account closes channel");

    let account_ids_after_remove =
        account_manager.get_account_ids().await.expect("get account ids after remove");
    assert_eq!(account_ids_after_remove, Vec::<u64>::new());

    // Provision a new account again.
    let _account_proxy = test_pinweaver_provision_account(&account_manager).await;

    let account_ids_after_reprovision =
        account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids_after_reprovision, vec![1]);
}

#[fuchsia::test]
async fn test_pinweaver_consecutive_updates() {
    // Loop 100 times.
    let n = 100;

    // Set up the mock cr50 responses.
    let mut mock_builder = MockCr50AgentBuilder::new()
        .add_reset_tree_response([0; 32])
        // During account provisioning PwAuth also makes an InsertLeaf and a TryAuth call.
        .add_insert_leaf_response([0; 32], [1; 32], vec![0, 1, 2, 3])
        .add_try_auth_success_response([0; 32], vec![0, 1, 2, 3], [1; 32]);
    for _ in 0..n {
        mock_builder = mock_builder
            .add_try_auth_failed_response([0; 32], vec![2, 3, 4, 5], [1; 32])
            .add_try_auth_rate_limited_response(1)
            .add_try_auth_success_response([0; 32], vec![0, 1, 2, 3], [1; 32]);
    }
    let mocks = mock_builder.build();
    let env = TestEnv::build_with_cr50_mock(Config::PinweaverOrScrypt, Some(mocks)).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    // Provision an account.
    let _account_proxy = test_pinweaver_provision_account(&account_manager).await;

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);

    // Do the update loop N times
    for _ in 0..n {
        test_pinweaver_failed_auth(&account_manager).await;
        test_pinweaver_rate_limited_auth(&account_manager).await;
        test_pinweaver_successful_auth(&account_manager).await;
    }
}

#[fuchsia::test]
async fn test_pinweaver_unknown_label() {
    // Set up the mock cr50 responses.
    let mocks = MockCr50AgentBuilder::new()
        .add_reset_tree_response([0; 32])
        // During account provisioning PwAuth also makes an InsertLeaf and a TryAuth call.
        .add_insert_leaf_response([0; 32], [1; 32], vec![0, 1, 2, 3])
        .add_try_auth_success_response([0; 32], vec![0, 1, 2, 3], [1; 32])
        // Call remove_credential manually.
        .add_remove_leaf_response([0; 32])
        .build();
    let env = TestEnv::build_with_cr50_mock(Config::PinweaverOrScrypt, Some(mocks)).await;
    let _ramdisk = setup_ramdisk(&env.realm_instance, FUCHSIA_DATA_GUID, ACCOUNT_LABEL).await;
    let account_manager = env.account_manager();

    // Provision an account.
    let _account_proxy = test_pinweaver_provision_account(&account_manager).await;

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1]);

    // Remove the label from credential manager directly.
    let cred_manager = env.credential_manager();

    // We know the label to remove is 0 because the first leaf populated in
    // the hash tree is always 0.
    cred_manager
        .remove_credential(0)
        .await
        .expect("remove_credential FIDL")
        .expect("remove_credential");
    drop(cred_manager);

    // Try to authenticate but should get a failure.
    let (_account_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    account_manager
        .deprecated_get_account(GLOBAL_ACCOUNT_ID, REAL_PASSWORD, server_end)
        .await
        .expect("deprecated_get_account FIDL")
        .expect_err("deprecated_get_account");
}
