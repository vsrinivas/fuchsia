// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{create_proxy, ServiceMarker},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_examples as fexamples, fidl_fuchsia_io as fio,
    fuchsia_component::client,
    itertools::Itertools,
    std::collections::HashMap,
    tracing::*,
};

/// Child component which provides the echo service.
const PROVIDER_CHILD_NAME: &str = "echo_provider_a";

/// Component URL of the service client component.
const CLIENT_COMPONENT_URL: &str = "#meta/echo-service-client.cm";

/// Component URL of the service provider component.
const PROVIDER_COMPONENT_URL: &str = "#meta/multi-instance-echo-provider.cm";

/// Name of the collection in the test root component that contains components that use the filtered Echo service.
const TEST_COLLECTION_NAME: &str = "test_collection";

/*
Test that dynamic child is offered a multi instance service with one instance renamed correctly
┌───────────────┐
│   test_root   │
└┬─────────────┬┘
 │            ┌┴─────────┐
 │            │Collection│
 │            └────────┬─┘
┌┴────────────┐        │
│echo_provider│        │
└─────────────┘        │
                ┌──────┴────────────┐
                │echo_client_renamed│
                └───────────────────┘
*/
#[fuchsia::test]
async fn renamed_instances_test() {
    let dynamic_child_name = "echo_client_renamed";
    let provider_exposed_dir =
        client::open_childs_exposed_directory(PROVIDER_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&provider_exposed_dir).await;

    let expected_renamed_instance_list = vec![
        "goodbye".to_string(),
        "hello".to_string(),
        "renamed_default".to_string(),
        "renamed_default_again".to_string(),
    ];

    let renamed_instances = vec![
        fdecl::NameMapping {
            source_name: "default".to_string(),
            target_name: "renamed_default".to_string(),
        },
        fdecl::NameMapping {
            source_name: "default".to_string(),
            target_name: "renamed_default_again".to_string(),
        },
    ];
    let provider_source = fdecl::Ref::Child(fdecl::ChildRef {
        name: PROVIDER_CHILD_NAME.to_string(),
        collection: None,
    });
    create_dynamic_service_client(
        dynamic_child_name,
        provider_source,
        Some(renamed_instances),
        None,
    )
    .await;
    let filtered_exposed_dir = client::open_childs_exposed_directory(
        dynamic_child_name,
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let renamed_service_dir_proxy =
        client::open_service_at_dir::<fexamples::EchoServiceMarker>(&filtered_exposed_dir)
            .expect("failed to open service in expose dir.");
    let visible_service_instances: Vec<String> = files_async::readdir(&renamed_service_dir_proxy)
        .await
        .expect("failed to read entries from exposed service dir")
        .into_iter()
        .map(|dirent| dirent.name)
        .collect();
    info!("Entries in exposed service dir after rename: {:?}", visible_service_instances);
    assert_eq!(expected_renamed_instance_list, visible_service_instances);

    // Test that the instances are renamed as expected
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default", "hello world!")
            .await
            .unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "renamed_default", "hello world!")
            .await
            .unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default", "hello world!")
            .await
            .unwrap(),
        regular_echo_at_service_instance(
            &filtered_exposed_dir,
            "renamed_default_again",
            "hello world!"
        )
        .await
        .unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "hello", "hello world!")
            .await
            .unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello", "hello world!")
            .await
            .unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "goodbye", "hello world!")
            .await
            .unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "goodbye", "hello world!")
            .await
            .unwrap(),
    );

    // Test that the original name of the renamed instance is no longer accessible from the filtered service
    assert!(matches!(
        regular_echo_at_service_instance(&filtered_exposed_dir, "default", "hello world!").await,
        Err(_)
    ));
}

/*
Test that dynamic child is offered a multi instance service with only one instance visible correctly
┌───────────────┐
│   test_root   │
└┬─────────────┬┘
 │            ┌┴─────────┐
 │            │Collection│
 │            └────────┬─┘
┌┴────────────┐        │
│echo_provider│        │
└─────────────┘        │
                ┌──────┴─────────────┐
                │echo_client_filtered│
                └────────────────────┘
*/
#[fuchsia::test]
async fn filter_instances_test() {
    let dynamic_child_name = "echo_client_filtered";
    let provider_exposed_dir =
        client::open_childs_exposed_directory(PROVIDER_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");
    let _ = verify_original_service(&provider_exposed_dir).await;

    // create a child with an offer to use the service exposed from one of the prviders with a renamed instance
    let source_instance_filter = vec!["default".to_string(), "goodbye".to_string()];
    let provider_source = fdecl::Ref::Child(fdecl::ChildRef {
        name: PROVIDER_CHILD_NAME.to_string(),
        collection: None,
    });
    create_dynamic_service_client(
        dynamic_child_name,
        provider_source,
        None,
        Some(source_instance_filter.clone()),
    )
    .await;
    //let filtered_exposed_dir = get_child_exposed_dir(dynamic_child_name).await;
    let filtered_exposed_dir = client::open_childs_exposed_directory(
        dynamic_child_name,
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let filtered_service_dir_proxy =
        client::open_service_at_dir::<fexamples::EchoServiceMarker>(&filtered_exposed_dir)
            .expect("failed to open service in expose dir.");
    let visible_service_instances: Vec<String> = files_async::readdir(&filtered_service_dir_proxy)
        .await
        .expect("failed to read entries from exposed service dir")
        .into_iter()
        .map(|dirent| dirent.name)
        .collect();
    info!("Entries in exposed service dir after filter: {:?}", visible_service_instances);
    assert_eq!(source_instance_filter, visible_service_instances);

    // Test that the instances are filtered as expected
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default", "hello world!")
            .await
            .unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "default", "hello world!")
            .await
            .unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "goodbye", "hello world!")
            .await
            .unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "goodbye", "hello world!")
            .await
            .unwrap(),
    );

    // Test that the original name of the renamed instance is no longer accessible from the filtered service
    assert!(matches!(
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello", "hello world!").await,
        Err(_)
    ));
}

/* Test that a filtered service offered from one dynamically created child to another works correctly, with instances both renamed and filtered.
*

┌─────────┐
│test_root│
└┬────────┘
┌┴────────────────────────┐
│      Collection         │
└┬───────────────────────┬┘
┌┴────────────────────┐ ┌┴───────────────────────────────┐
│dynamic_echo_provider│ │echo_client_filtered_and_renamed│
└─────────────────────┘ └────────────────────────────────┘
*/
#[fuchsia::test]
async fn filtered_service_through_collection_test() {
    let dynamic_child_name = "echo_client_filtered_and_renamed";
    let dynamic_child_provider_name = "dynamic_echo_provider";
    let static_provider_exposed_dir =
        client::open_childs_exposed_directory(PROVIDER_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");
    let original_service_instances = verify_original_service(&static_provider_exposed_dir).await;

    // create a child with an offer to use the service exposed from one of the prviders with a renamed instance
    let source_instance_filter = vec!["renamed_default".to_string(), "goodbye".to_string()];
    let original_to_renamed = HashMap::from([
        ("default".to_string(), "renamed_default".to_string()),
        ("goodbye".to_string(), "goodbye".to_string()),
        ("hello".to_string(), "hello".to_string()),
    ]);
    let expected_renamed_instance_list: Vec<String> = original_service_instances
        .clone()
        .into_iter()
        .filter_map(|n| original_to_renamed.get(&n))
        .map(|s| s.clone())
        .filter(|n| source_instance_filter.contains(&n))
        .sorted()
        .collect();

    let renamed_instances = {
        let mut renaming = vec![];
        for (k, v) in &original_to_renamed {
            if k != v {
                renaming
                    .push(fdecl::NameMapping { source_name: k.clone(), target_name: v.clone() });
            }
        }
        Some(renaming)
    };

    create_dynamic_service_provider(dynamic_child_provider_name).await;
    let mut dynamic_provider_child_ref = fdecl::ChildRef {
        name: dynamic_child_provider_name.to_string(),
        collection: Some(TEST_COLLECTION_NAME.to_string()),
    };
    let provider_source = fdecl::Ref::Child(dynamic_provider_child_ref.clone());

    let (provider_exposed_dir, provider_server) =
        create_proxy::<fio::DirectoryMarker>().expect("Failed to create directory proxy");
    let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .expect("could not connect to Realm service");
    let _ = realm
        .open_exposed_dir(&mut dynamic_provider_child_ref, provider_server)
        .await
        .expect("OpenExposedDir FIDL failed.");

    let dynamic_provider_unfiltered_instances =
        verify_original_service(&static_provider_exposed_dir).await;
    assert_eq!(dynamic_provider_unfiltered_instances, original_service_instances);

    create_dynamic_service_client(
        dynamic_child_name,
        provider_source,
        renamed_instances,
        Some(source_instance_filter.clone()),
    )
    .await;
    //let filtered_exposed_dir = get_child_exposed_dir(dynamic_child_name).await;
    let filtered_exposed_dir = client::open_childs_exposed_directory(
        dynamic_child_name,
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let filtered_service_dir_proxy =
        client::open_service_at_dir::<fexamples::EchoServiceMarker>(&filtered_exposed_dir)
            .expect("failed to open service in expose dir.");
    let visible_service_instances: Vec<String> = files_async::readdir(&filtered_service_dir_proxy)
        .await
        .expect("failed to read entries from exposed service dir")
        .into_iter()
        .map(|dirent| dirent.name)
        .collect();
    info!("Entries in exposed service dir after filter: {:?}", visible_service_instances);

    assert_eq!(expected_renamed_instance_list, visible_service_instances);

    // Test that the instances are filtered as expected
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default", "hello world!")
            .await
            .unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "renamed_default", "hello world!")
            .await
            .unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "goodbye", "hello world!")
            .await
            .unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "goodbye", "hello world!")
            .await
            .unwrap(),
    );

    // Test that the original name of the renamed instance is no longer accessible from the filtered service
    assert!(matches!(
        regular_echo_at_service_instance(&filtered_exposed_dir, "default", "hello world!").await,
        Err(_)
    ));
    assert!(matches!(
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello", "hello world!").await,
        Err(_)
    ));
}

// Send the echo_string as the request to the regular echo protocol which is part of the EchoService.
async fn regular_echo_at_service_instance(
    exposed_dir: &fio::DirectoryProxy,
    instance_name: &str,
    echo_string: &str,
) -> Result<String, fidl::Error> {
    let service_instance =
        client::connect_to_service_instance_at_dir::<fexamples::EchoServiceMarker>(
            exposed_dir,
            instance_name,
        )
        .expect("failed to connect to filtered service instance");

    let response = service_instance
        .regular_echo()
        .expect("failed to connect to regular_echo member from original service")
        .echo_string(echo_string)
        .await;

    // Ensure that calling the protocol directly produces the same result as opening the
    // service instance directory first, then connecting to the protocol through the open
    // directory.
    let direct_protocol_response =
        client::connect_to_named_protocol_at_dir_root::<fexamples::EchoMarker>(
            exposed_dir,
            vec!["fuchsia.examples.EchoService", instance_name, "regular_echo"].join("/").as_str(),
        )
        .expect("failed to connect to protocol directly")
        .echo_string(echo_string)
        .await;
    assert_eq!(response.clone().ok(), direct_protocol_response.ok());

    return response;
}

// Confirm that the original service exposes the expected service instances.
async fn verify_original_service(exposed_dir: &fio::DirectoryProxy) -> Vec<String> {
    let service_proxy = client::open_service_at_dir::<fexamples::EchoServiceMarker>(&exposed_dir)
        .expect("failed to open service in expose dir.");
    let instances = files_async::readdir(&service_proxy)
        .await
        .expect("failed to read entries from service dir")
        .into_iter()
        .map(|dirent| dirent.name);
    // If the source component definition has changed then update this assertion.
    let instance_list = instances.clone().collect::<Vec<String>>();
    assert_eq!(
        vec!["default".to_string(), "goodbye".to_string(), "hello".to_string()],
        instance_list
    );
    assert_eq!(
        regular_echo_at_service_instance(&exposed_dir, "default", "hello world!").await.unwrap(),
        "hello world!"
    );
    assert_eq!(
        regular_echo_at_service_instance(&exposed_dir, "hello", "hello world!").await.unwrap(),
        "hellohello world!"
    );
    assert_eq!(
        regular_echo_at_service_instance(&exposed_dir, "goodbye", "hello world!").await.unwrap(),
        "goodbyehello world!"
    );
    instance_list
}

async fn create_dynamic_service_client(
    child_name: &str,
    source_ref: fdecl::Ref,
    renamed_instances: Option<Vec<fdecl::NameMapping>>,
    source_instance_filter: Option<Vec<String>>,
) {
    let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .expect("could not connect to Realm service");
    let offer_service_decl = fdecl::OfferService {
        source: Some(source_ref),
        source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
        target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
        renamed_instances,
        source_instance_filter,
        ..fdecl::OfferService::EMPTY
    };
    let mut collection_ref = fdecl::CollectionRef { name: TEST_COLLECTION_NAME.to_string() };
    let child_decl = fdecl::Child {
        name: Some(child_name.to_string()),
        url: Some(CLIENT_COMPONENT_URL.to_string()),
        //url: Some("".to_string()),
        startup: Some(fdecl::StartupMode::Lazy),
        ..fdecl::Child::EMPTY
    };
    let dynamic_offers = vec![fdecl::Offer::Service(offer_service_decl)];
    let _ = realm
        .create_child(
            &mut collection_ref,
            child_decl,
            fcomponent::CreateChildArgs {
                numbered_handles: None,
                dynamic_offers: Some(dynamic_offers),
                ..fcomponent::CreateChildArgs::EMPTY
            },
        )
        .await
        .expect("Failed to create dynamic child service client.");
}

async fn create_dynamic_service_provider(child_name: &str) {
    let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .expect("could not connect to Realm service");
    let mut collection_ref = fdecl::CollectionRef { name: TEST_COLLECTION_NAME.to_string() };
    let child_decl = fdecl::Child {
        name: Some(child_name.to_string()),
        url: Some(PROVIDER_COMPONENT_URL.to_string()),
        startup: Some(fdecl::StartupMode::Lazy),
        ..fdecl::Child::EMPTY
    };
    let _ = realm
        .create_child(
            &mut collection_ref,
            child_decl,
            fcomponent::CreateChildArgs { ..fcomponent::CreateChildArgs::EMPTY },
        )
        .await
        .expect("Failed to create dynamic child service client.");
}
