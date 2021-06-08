// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tests {
    use {
        crate::routing::RoutingTestBuilderForAnalyzer,
        routing_test_helpers::storage_admin::CommonStorageAdminTest,
    };

    #[fuchsia::test]
    async fn storage_to_one_child_admin_to_another() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_to_one_child_admin_to_another()
            .await
    }

    #[fuchsia::test]
    async fn directory_from_grandparent_storage_and_admin_from_parent() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_directory_from_grandparent_storage_and_admin_from_parent()
            .await
    }

    #[fuchsia::test]
    async fn storage_admin_from_sibling() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_admin_from_sibling()
            .await;
    }

    #[fuchsia::test]
    async fn admin_protocol_used_in_the_same_place_storage_is_declared() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_admin_protocol_used_in_the_same_place_storage_is_declared()
            .await;
    }

    #[fuchsia::test]
    async fn storage_admin_from_protocol_on_self() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_admin_from_protocol_on_self()
            .await;
    }

    #[fuchsia::test]
    async fn storage_admin_from_protocol_from_parent() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_admin_from_protocol_from_parent()
            .await;
    }

    #[fuchsia::test]
    async fn storage_admin_from_protocol_on_sibling() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_admin_from_protocol_on_sibling()
            .await;
    }

    #[fuchsia::test]
    async fn storage_admin_from_storage_on_self_bad_protocol_name() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_admin_from_storage_on_self_bad_protocol_name()
            .await;
    }

    #[fuchsia::test]
    async fn storage_admin_from_storage_on_parent_bad_protocol_name() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_admin_from_storage_on_parent_bad_protocol_name()
            .await;
    }

    #[fuchsia::test]
    async fn storage_admin_from_protocol_on_sibling_bad_protocol_name() {
        CommonStorageAdminTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_storage_admin_from_protocol_on_sibling_bad_protocol_name()
            .await;
    }
}
