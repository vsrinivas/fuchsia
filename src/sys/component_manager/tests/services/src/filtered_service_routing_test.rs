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
const PROVIDER_A_CHILD_NAME: &str = "echo_provider_a";

/// Another component which provides the echo service.
const PROVIDER_B_CHILD_NAME: &str = "echo_provider_b";

/// Component URL of the service client component.
const CLIENT_COMPONENT_URL: &str = "#meta/echo-service-client.cm";

/// Component URL of the service provider component.
const PROVIDER_COMPONENT_URL: &str = "#meta/multi-instance-echo-provider.cm";

/// Name of the collection in the test root component that contains components that use the filtered Echo service.
const TEST_COLLECTION_NAME: &str = "test_collection";

/// Test string used to send to the echo service under test.
const ECHO_TEST_STRING: &str = "hello world";

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
        client::open_childs_exposed_directory(PROVIDER_A_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&provider_exposed_dir).await;

    let expected_visible_instance_list = vec![
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
        name: PROVIDER_A_CHILD_NAME.to_string(),
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
    let visible_service_instances: Vec<String> =
        fuchsia_fs::directory::readdir(&renamed_service_dir_proxy)
            .await
            .expect("failed to read entries from exposed service dir")
            .into_iter()
            .map(|dirent| dirent.name)
            .collect();
    info!("Entries in exposed service dir after rename: {:?}", visible_service_instances);
    assert_eq!(expected_visible_instance_list, visible_service_instances);

    // Test that the instances are renamed as expected
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "renamed_default").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "renamed_default_again")
            .await
            .unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "hello").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "goodbye").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "goodbye").await.unwrap(),
    );

    // Test that the original name of the renamed instance is no longer accessible from the filtered service
    assert!(matches!(
        regular_echo_at_service_instance(&filtered_exposed_dir, "default").await,
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
        client::open_childs_exposed_directory(PROVIDER_A_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");
    let _ = verify_original_service(&provider_exposed_dir).await;

    // create a child with an offer to use the service exposed from one of the prviders with a renamed instance
    let source_instance_filter = vec!["default".to_string(), "goodbye".to_string()];
    let provider_source = fdecl::Ref::Child(fdecl::ChildRef {
        name: PROVIDER_A_CHILD_NAME.to_string(),
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
    let visible_service_instances: Vec<String> =
        fuchsia_fs::directory::readdir(&filtered_service_dir_proxy)
            .await
            .expect("failed to read entries from exposed service dir")
            .into_iter()
            .map(|dirent| dirent.name)
            .collect();
    info!("Entries in exposed service dir after filter: {:?}", visible_service_instances);
    assert_eq!(source_instance_filter, visible_service_instances);

    // Test that the instances are filtered as expected
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "default").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "goodbye").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "goodbye").await.unwrap(),
    );

    // Test that the original name of the renamed instance is no longer accessible from the filtered service
    assert!(matches!(
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello").await,
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
        client::open_childs_exposed_directory(PROVIDER_A_CHILD_NAME.to_string(), None)
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
    let expected_visible_instance_list: Vec<String> = original_service_instances
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
    let visible_service_instances: Vec<String> =
        fuchsia_fs::directory::readdir(&filtered_service_dir_proxy)
            .await
            .expect("failed to read entries from exposed service dir")
            .into_iter()
            .map(|dirent| dirent.name)
            .collect();
    info!("Entries in exposed service dir after filter: {:?}", visible_service_instances);

    assert_eq!(expected_visible_instance_list, visible_service_instances);

    // Test that the instances are filtered as expected
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "renamed_default").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "goodbye").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "goodbye").await.unwrap(),
    );

    // Test that the original name of the renamed instance is no longer accessible from the filtered service
    assert!(matches!(
        regular_echo_at_service_instance(&filtered_exposed_dir, "default").await,
        Err(_)
    ));
    assert!(matches!(
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello").await,
        Err(_)
    ));
}

#[fuchsia::test]
async fn aggregate_instances_test() {
    let dynamic_child_name = "aggregate_instances_test_client";
    let provider_exposed_dir =
        client::open_childs_exposed_directory(PROVIDER_A_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&provider_exposed_dir).await;
    let dynamic_provider_name = "dynamic_aggregate_instances_test_provider";
    create_dynamic_service_provider(dynamic_provider_name).await;
    let dynamic_provider_exposed_dir = client::open_childs_exposed_directory(
        dynamic_provider_name.to_string(),
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&dynamic_provider_exposed_dir).await;

    let offer_service_decl_0 = fdecl::OfferService {
        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
            name: PROVIDER_A_CHILD_NAME.to_string(),
            collection: None,
        })),
        source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
        target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
        renamed_instances: None,
        source_instance_filter: Some(vec!["default".to_string()]),
        ..fdecl::OfferService::EMPTY
    };
    let offer_service_decl_1 = fdecl::OfferService {
        source: Some(fdecl::Ref::Child(fdecl::ChildRef {
            name: dynamic_provider_name.to_string(),
            collection: Some(TEST_COLLECTION_NAME.to_string()),
        })),
        source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
        target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
        renamed_instances: None,
        source_instance_filter: Some(vec!["goodbye".to_string()]),
        ..fdecl::OfferService::EMPTY
    };

    let dynamic_offers = vec![
        fdecl::Offer::Service(offer_service_decl_0),
        fdecl::Offer::Service(offer_service_decl_1),
    ];
    create_dynamic_service_client_from_offers(dynamic_child_name, dynamic_offers)
        .await
        .expect("Failed to create dynamic service client");
    let filtered_exposed_dir = client::open_childs_exposed_directory(
        dynamic_child_name,
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let filtered_service_dir_proxy =
        client::open_service_at_dir::<fexamples::EchoServiceMarker>(&filtered_exposed_dir)
            .expect("failed to open service in expose dir.");
    let visible_service_instances: Vec<String> =
        fuchsia_fs::directory::readdir(&filtered_service_dir_proxy)
            .await
            .expect("failed to read entries from exposed service dir")
            .into_iter()
            .map(|dirent| dirent.name)
            .collect();
    info!("Entries in exposed service dir after aggregation: {:?}", visible_service_instances);
    let expected_visible_instance_list: Vec<String> =
        vec!["default", "goodbye"].iter().map(|s| s.to_string()).collect();
    assert_eq!(expected_visible_instance_list, visible_service_instances);

    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default",).await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "default").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "goodbye").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "goodbye").await.unwrap(),
    );
}

#[fuchsia::test]
async fn aggregate_instances_renamed_test() {
    let dynamic_child_name = "aggregate_instances_renamed_test_client";
    let provider_exposed_dir =
        client::open_childs_exposed_directory(PROVIDER_A_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&provider_exposed_dir).await;
    let dynamic_provider_name = "aggregate_instances_renamed_test_provider";
    create_dynamic_service_provider(dynamic_provider_name).await;
    let dynamic_provider_exposed_dir = client::open_childs_exposed_directory(
        dynamic_provider_name.to_string(),
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&dynamic_provider_exposed_dir).await;
    let offers = vec![
        fdecl::Offer::Service(fdecl::OfferService {
            source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                name: PROVIDER_A_CHILD_NAME.to_string(),
                collection: None,
            })),
            source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            renamed_instances: Some(vec![fdecl::NameMapping {
                source_name: "default".to_string(),
                target_name: "default_from_a".to_string(),
            }]),
            source_instance_filter: Some(vec!["default_from_a".to_string()]),
            ..fdecl::OfferService::EMPTY
        }),
        fdecl::Offer::Service(fdecl::OfferService {
            source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                name: dynamic_provider_name.to_string(),
                collection: Some(TEST_COLLECTION_NAME.to_string()),
            })),
            source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            renamed_instances: Some(vec![fdecl::NameMapping {
                source_name: "hello".to_string(),
                target_name: "hello_from_dynamic".to_string(),
            }]),
            source_instance_filter: Some(vec!["hello_from_dynamic".to_string()]),
            ..fdecl::OfferService::EMPTY
        }),
    ];
    create_dynamic_service_client_from_offers(dynamic_child_name, offers)
        .await
        .expect("Failed to create dynamic service client");
    let filtered_exposed_dir = client::open_childs_exposed_directory(
        dynamic_child_name,
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let renamed_service_dir_proxy =
        client::open_service_at_dir::<fexamples::EchoServiceMarker>(&filtered_exposed_dir)
            .expect("failed to open service in expose dir.");
    let visible_service_instances: Vec<String> =
        fuchsia_fs::directory::readdir(&renamed_service_dir_proxy)
            .await
            .expect("failed to read entries from exposed service dir")
            .into_iter()
            .map(|dirent| dirent.name)
            .collect();
    info!("Entries in exposed service dir after rename: {:?}", visible_service_instances);
    let expected_visible_instance_list: Vec<String> =
        vec!["default_from_a", "hello_from_dynamic"].iter().map(|s| s.to_string()).collect();
    assert_eq!(expected_visible_instance_list, visible_service_instances);

    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "default").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "default_from_a").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "hello").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello_from_dynamic",)
            .await
            .unwrap(),
    );
}

// aggregate_instances_multiple_service_instances_renamed_test tests that an aggregate service
// where the component sources that are aggregated supply multiple service instances are aggregated
// correctly.
// The components implementing the EchoService each implement 3 service instances "default" , "hello", and "goodbye".
// This test asserts the resulting namespace in the component that "use"s the EchoService contains only the service
// instances that exist in ther source_instance_filters of the aggregated offers, some of which are renamed, and that
// they respond with the correct response when the regular_echo protocol is called on each service instance.
#[fuchsia::test]
async fn aggregate_instances_multiple_service_instances_renamed_test() {
    let dynamic_child_name = "aggregate_instances_multiple_service_instances_renamed_test_client";
    let provider_exposed_dir =
        client::open_childs_exposed_directory(PROVIDER_A_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&provider_exposed_dir).await;
    let dynamic_provider_name = "aggregate_instances_renamed_test_provider";
    create_dynamic_service_provider(dynamic_provider_name).await;
    let dynamic_provider_exposed_dir = client::open_childs_exposed_directory(
        dynamic_provider_name.to_string(),
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&dynamic_provider_exposed_dir).await;
    let offers = vec![
        fdecl::Offer::Service(fdecl::OfferService {
            source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                name: PROVIDER_A_CHILD_NAME.to_string(),
                collection: None,
            })),
            source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            // Test that renaming a service instance to the identical name as one of
            // the other original instances results in only the rename instance being
            // visible in the filtered service and that renaming an instance twice results
            // in 2 instances in the filtered service that refer to the same underlying
            // instance.
            renamed_instances: Some(vec![
                fdecl::NameMapping {
                    source_name: "hello".to_string(),
                    target_name: "default".to_string(),
                },
                fdecl::NameMapping {
                    source_name: "hello".to_string(),
                    target_name: "hello_v1".to_string(),
                },
            ]),
            source_instance_filter: Some(vec!["default".to_string(), "hello_v1".to_string()]),
            ..fdecl::OfferService::EMPTY
        }),
        fdecl::Offer::Service(fdecl::OfferService {
            source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                name: PROVIDER_B_CHILD_NAME.to_string(),
                collection: None,
            })),
            source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            renamed_instances: Some(vec![fdecl::NameMapping {
                // Test a simple one instance rename to a unique new name.
                source_name: "hello".to_string(),
                target_name: "hello_v2".to_string(),
            }]),
            source_instance_filter: Some(vec!["hello_v2".to_string()]),
            ..fdecl::OfferService::EMPTY
        }),
        fdecl::Offer::Service(fdecl::OfferService {
            source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                name: dynamic_provider_name.to_string(),
                collection: Some(TEST_COLLECTION_NAME.to_string()),
            })),
            source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            // Test that a rename of a service instance to an identical name results
            // in a noop and that the instance is visible in the filtered service.
            renamed_instances: Some(vec![fdecl::NameMapping {
                source_name: "goodbye".to_string(),
                target_name: "goodbye".to_string(),
            }]),
            source_instance_filter: Some(vec!["goodbye".to_string()]),
            ..fdecl::OfferService::EMPTY
        }),
    ];
    create_dynamic_service_client_from_offers(dynamic_child_name, offers)
        .await
        .expect("Failed to create dynamic service client");
    let filtered_exposed_dir = client::open_childs_exposed_directory(
        dynamic_child_name,
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let renamed_service_dir_proxy =
        client::open_service_at_dir::<fexamples::EchoServiceMarker>(&filtered_exposed_dir)
            .expect("failed to open service in expose dir.");
    let visible_service_instances: Vec<String> =
        fuchsia_fs::directory::readdir(&renamed_service_dir_proxy)
            .await
            .expect("failed to read entries from exposed service dir")
            .into_iter()
            .map(|dirent| dirent.name)
            .collect();
    info!("Entries in exposed service dir after rename: {:?}", visible_service_instances);
    let expected_visible_instance_list: Vec<String> =
        vec!["default", "goodbye", "hello_v1", "hello_v2"].iter().map(|s| s.to_string()).collect();
    assert_eq!(expected_visible_instance_list, visible_service_instances);

    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "hello").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "default").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "hello").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello_v1").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "hello").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "hello_v2").await.unwrap(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&provider_exposed_dir, "goodbye").await.unwrap(),
        regular_echo_at_service_instance(&filtered_exposed_dir, "goodbye",).await.unwrap(),
    );
}

#[fuchsia::test]
async fn aggregate_service_fails_without_filter_test() {
    let dynamic_child_name = "aggregate_service_fails_without_filter_test_client";
    let provider_exposed_dir =
        client::open_childs_exposed_directory(PROVIDER_A_CHILD_NAME.to_string(), None)
            .await
            .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&provider_exposed_dir).await;
    let dynamic_provider_name = "aggregate_instances_renamed_test_provider";
    create_dynamic_service_provider(dynamic_provider_name).await;
    let dynamic_provider_exposed_dir = client::open_childs_exposed_directory(
        dynamic_provider_name.to_string(),
        Some(TEST_COLLECTION_NAME.to_string()),
    )
    .await
    .expect("Failed to get child expose directory.");

    let _ = verify_original_service(&dynamic_provider_exposed_dir).await;
    let offers = vec![
        fdecl::Offer::Service(fdecl::OfferService {
            source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                name: PROVIDER_A_CHILD_NAME.to_string(),
                collection: None,
            })),
            source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            renamed_instances: None,
            source_instance_filter: None,
            ..fdecl::OfferService::EMPTY
        }),
        fdecl::Offer::Service(fdecl::OfferService {
            source: Some(fdecl::Ref::Child(fdecl::ChildRef {
                name: dynamic_provider_name.to_string(),
                collection: Some(TEST_COLLECTION_NAME.to_string()),
            })),
            source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
            renamed_instances: None,
            source_instance_filter: None,
            ..fdecl::OfferService::EMPTY
        }),
    ];
    let create_child_result =
        create_dynamic_service_client_from_offers(dynamic_child_name, offers).await;
    // Creating this child is intended to fail since the source_instance_filter is not set on at least one of the offers.
    assert!(matches!(create_child_result, Err(_)));
}

// Send the echo_string as the request to the regular echo protocol which is part of the EchoService.
async fn regular_echo_at_service_instance(
    exposed_dir: &fio::DirectoryProxy,
    instance_name: &str,
) -> Result<String, fidl::Error> {
    let echo_string = ECHO_TEST_STRING;
    let service_instance =
        client::connect_to_service_instance_at_dir::<fexamples::EchoServiceMarker>(
            exposed_dir,
            instance_name,
        )
        .expect("failed to connect to filtered service instance");

    let response = async {
        // Accessing a non-existent service instance can fail when opening the member protocol
        // or calling a method on the protocol, depending on when the underlying channel used to open the
        // service capability is closed.
        let proxy = service_instance.regular_echo()?;
        proxy.echo_string(echo_string).await
    }
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
    let instances = fuchsia_fs::directory::readdir(&service_proxy)
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
        regular_echo_at_service_instance(&exposed_dir, "default").await.unwrap(),
        ECHO_TEST_STRING.to_string(),
    );
    assert_eq!(
        regular_echo_at_service_instance(&exposed_dir, "hello",).await.unwrap(),
        format!("hello{}", ECHO_TEST_STRING)
    );
    assert_eq!(
        regular_echo_at_service_instance(&exposed_dir, "goodbye").await.unwrap(),
        format!("goodbye{}", ECHO_TEST_STRING)
    );
    instance_list
}

async fn create_dynamic_service_client_from_offers(
    child_name: &str,
    dynamic_offers: Vec<fdecl::Offer>,
) -> Result<(), fcomponent::Error> {
    let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .expect("could not connect to Realm service");
    let mut collection_ref = fdecl::CollectionRef { name: TEST_COLLECTION_NAME.to_string() };
    let child_decl = fdecl::Child {
        name: Some(child_name.to_string()),
        url: Some(CLIENT_COMPONENT_URL.to_string()),
        //url: Some("".to_string()),
        startup: Some(fdecl::StartupMode::Lazy),
        ..fdecl::Child::EMPTY
    };
    realm
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
        .expect("Failed to create dynamiic child service client.")
}

async fn create_dynamic_service_client(
    child_name: &str,
    source_ref: fdecl::Ref,
    renamed_instances: Option<Vec<fdecl::NameMapping>>,
    source_instance_filter: Option<Vec<String>>,
) {
    let offer_service_decl = fdecl::OfferService {
        source: Some(source_ref),
        source_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
        target_name: Some(fexamples::EchoServiceMarker::SERVICE_NAME.to_string()),
        renamed_instances,
        source_instance_filter,
        ..fdecl::OfferService::EMPTY
    };

    let dynamic_offers = vec![fdecl::Offer::Service(offer_service_decl)];
    create_dynamic_service_client_from_offers(child_name, dynamic_offers)
        .await
        .expect("Failed to create dynamic service client")
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
