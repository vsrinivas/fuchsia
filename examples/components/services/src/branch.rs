// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This program launches `fuchsia.examples.services.BankAccount` service providers and consumes
//! their instances.
//!
//! This program is written as a test so that it can be easily launched with `fx test`.

use {
    fidl_fuchsia_examples_services as fexamples, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2 as fsys2, fuchsia_component::client::connect_to_service_instance, log::*,
};

const COLLECTION_NAME: &'static str = "account_providers";
const TEST_PACKAGE: &'static str = "fuchsia-pkg://fuchsia.com/service-examples";

#[fuchsia::test]
async fn read_and_write_to_multiple_service_instances() {
    // Launch two BankAccount providers into the `account_providers` collection.
    let realm = fuchsia_component::client::connect_to_protocol::<fsys2::RealmMarker>()
        .expect("connect to Realm service");
    start_provider(&realm, "a", &format!("{}#meta/provider-a.cm", TEST_PACKAGE)).await;
    start_provider(&realm, "b", &format!("{}#meta/provider-b.cm", TEST_PACKAGE)).await;

    // Debit both bank accounts by $5.
    for (account, expected_owner) in &[("a", "A"), ("b", "B")] {
        let proxy = connect_to_service_instance::<fexamples::BankAccountMarker>(&format!(
            "{}/default",
            account
        ))
        .expect("failed to connect to service instance");
        let read_only_account = proxy.read_only().expect("read_only protocol");
        let owner = read_only_account.get_owner().await.expect("failed to get owner");
        assert_eq!(owner, *expected_owner);
        let initial_balance = read_only_account.get_balance().await.expect("failed to get_balance");
        info!("retrieved account for owner '{}' with balance ${}", &owner, &initial_balance);

        let read_write_account = proxy.read_write().expect("read_write protocol");
        assert_eq!(
            read_write_account.get_owner().await.expect("failed to get_owner"),
            *expected_owner
        );
        assert_eq!(
            read_write_account.get_balance().await.expect("failed to get_balance"),
            initial_balance
        );
        info!("debiting account of owner '{}'", &owner);
        read_write_account.debit(5).await.expect("failed to debit");
        assert_eq!(
            read_write_account.get_balance().await.expect("failed to get_balance"),
            initial_balance - 5
        );
    }
}

async fn start_provider(realm: &fsys2::RealmProxy, name: &str, url: &str) -> fio::DirectoryProxy {
    info!("creating BankAccount provider \"{}\" with url={}", name, url);
    realm
        .create_child(
            &mut fsys2::CollectionRef { name: COLLECTION_NAME.to_string() },
            fsys2::ChildDecl {
                name: Some(name.to_string()),
                url: Some(url.to_string()),
                startup: Some(fsys2::StartupMode::Lazy),
                environment: None,
                ..fsys2::ChildDecl::EMPTY
            },
        )
        .await
        .expect("failed to make create_child FIDL call")
        .expect("failed to create_child");

    let (exposed_dir, exposed_dir_server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("failed to create endpoints");
    info!("binding to BankAccount provider \"{}\" with url={}", name, url);
    realm
        .bind_child(
            &mut fsys2::ChildRef {
                name: name.to_string(),
                collection: Some(COLLECTION_NAME.to_string()),
            },
            exposed_dir_server_end,
        )
        .await
        .expect("failed to make bind_child FIDL call")
        .expect("failed to bind_child");
    exposed_dir
}
