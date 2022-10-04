// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tests {
    use {
        crate::routing::RoutingTestBuilderForAnalyzer,
        cm_moniker::InstancedRelativeMoniker,
        cm_rust::{
            Availability, OfferDecl, OfferSource, OfferStorageDecl, OfferTarget, StorageDecl,
            StorageDirectorySource, UseDecl, UseStorageDecl,
        },
        cm_rust_testing::{ComponentDeclBuilder, DirectoryDeclBuilder},
        component_id_index::gen_instance_id,
        fidl_fuchsia_component_decl as fdecl, fuchsia_zircon_status as zx_status,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMonikerBase},
        routing::rights::{READ_RIGHTS, WRITE_RIGHTS},
        routing_test_helpers::{
            component_id_index::make_index_file, storage::CommonStorageTest, CheckUse,
            ExpectedResult, RoutingTestModel, RoutingTestModelBuilder,
        },
        std::convert::TryInto,
    };

    #[fuchsia::test]
    async fn storage_dir_from_cm_namespace() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_dir_from_cm_namespace()
            .await
    }

    #[fuchsia::test]
    async fn storage_and_dir_from_parent() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_and_dir_from_parent()
            .await
    }

    #[fuchsia::test]
    async fn storage_and_dir_from_parent_with_subdir() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_and_dir_from_parent_with_subdir()
            .await
    }

    #[fuchsia::test]
    async fn storage_and_dir_from_parent_rights_invalid() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_and_dir_from_parent_rights_invalid()
            .await
    }

    #[fuchsia::test]
    async fn storage_from_parent_dir_from_grandparent() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_from_parent_dir_from_grandparent()
            .await
    }

    #[fuchsia::test]
    async fn storage_from_parent_dir_from_grandparent_with_subdirs() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_from_parent_dir_from_grandparent_with_subdirs()
            .await
    }

    #[fuchsia::test]
    async fn storage_from_parent_dir_from_grandparent_with_subdir() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_from_parent_dir_from_grandparent_with_subdir()
            .await
    }

    #[fuchsia::test]
    async fn storage_and_dir_from_grandparent() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_and_dir_from_grandparent()
            .await
    }

    #[fuchsia::test]
    async fn storage_from_parent_dir_from_sibling() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_from_parent_dir_from_sibling()
            .await
    }

    #[fuchsia::test]
    async fn storage_from_parent_dir_from_sibling_with_subdir() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_from_parent_dir_from_sibling_with_subdir()
            .await
    }

    #[fuchsia::test]
    async fn storage_multiple_types() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_multiple_types()
            .await
    }

    #[fuchsia::test]
    async fn use_the_wrong_type_of_storage() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_use_the_wrong_type_of_storage()
            .await
    }

    #[fuchsia::test]
    async fn directories_are_not_storage() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_directories_are_not_storage()
            .await
    }

    #[fuchsia::test]
    async fn use_storage_when_not_offered() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_use_storage_when_not_offered()
            .await
    }

    #[fuchsia::test]
    async fn dir_offered_from_nonexecutable() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_dir_offered_from_nonexecutable()
            .await
    }

    #[fuchsia::test]
    async fn storage_dir_from_cm_namespace_prevented_by_policy() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_dir_from_cm_namespace_prevented_by_policy()
            .await
    }

    #[fuchsia::test]
    async fn instance_id_from_index() {
        CommonStorageTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_instance_id_from_index()
            .await
    }

    ///   component manager's namespace
    ///    |
    ///   provider (provides storage capability, restricted to component ID index)
    ///    |
    ///   consumer (not in component ID index)
    ///
    /// Tests that consumer cannot use restricted storage as it isn't in the component ID
    /// index.
    ///
    /// This test only runs for the static model. Component Manager has a similar test that
    /// instead expects failure when a component is started, if that component uses restricted
    /// storage and is not in the component ID index.
    #[fuchsia::test]
    async fn use_restricted_storage_failure() {
        let parent_consumer_instance_id = Some(gen_instance_id(&mut rand::thread_rng()));
        let component_id_index_path = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: parent_consumer_instance_id.clone(),
                appmgr_moniker: None,
                moniker: Some(AbsoluteMoniker::parse_str("/parent_consumer").unwrap()),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();
        let components = vec![
            (
                "provider",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("data")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .storage(StorageDecl {
                        name: "cache".into(),
                        backing_dir: "data".try_into().unwrap(),
                        source: StorageDirectorySource::Self_,
                        subdir: None,
                        storage_id: fdecl::StorageId::StaticInstanceId,
                    })
                    .offer(OfferDecl::Storage(OfferStorageDecl {
                        source: OfferSource::Self_,
                        target: OfferTarget::static_child("consumer".to_string()),
                        source_name: "cache".into(),
                        target_name: "cache".into(),
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("consumer")
                    .build(),
            ),
            (
                "consumer",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Storage(UseStorageDecl {
                        source_name: "cache".into(),
                        target_path: "/storage".try_into().unwrap(),
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let mut builder = RoutingTestBuilderForAnalyzer::new("provider", components);
        builder.set_component_id_index_path(
            component_id_index_path.path().to_str().unwrap().to_string(),
        );
        let model = builder.build().await;

        model
            .check_use(
                AbsoluteMoniker::parse_str("/consumer").unwrap(),
                CheckUse::Storage {
                    path: "/storage".try_into().unwrap(),
                    storage_relation: Some(InstancedRelativeMoniker::new(
                        vec!["consumer:0".into()],
                    )),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::Err(zx_status::Status::UNAVAILABLE),
                },
            )
            .await;
    }
}
