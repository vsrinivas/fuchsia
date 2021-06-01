// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tests {
    use {
        crate::routing::RoutingTestBuilderForAnalyzer,
        routing_test_helpers::rights::CommonRightsTest,
    };

    #[fuchsia::test]
    async fn offer_increasing_rights() {
        CommonRightsTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_offer_increasing_rights()
            .await
    }

    #[fuchsia::test]
    async fn offer_incompatible_rights() {
        CommonRightsTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_offer_incompatible_rights()
            .await
    }

    #[fuchsia::test]
    async fn expose_increasing_rights() {
        CommonRightsTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_expose_increasing_rights()
            .await
    }

    #[fuchsia::test]
    async fn expose_incompatible_rights() {
        CommonRightsTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_expose_incompatible_rights()
            .await
    }

    #[fuchsia::test]
    async fn capability_increasing_rights() {
        CommonRightsTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_capability_increasing_rights()
            .await
    }

    #[fuchsia::test]
    async fn capability_incompatible_rights() {
        CommonRightsTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_capability_incompatible_rights()
            .await
    }

    #[fuchsia::test]
    async fn offer_from_component_manager_namespace_directory_incompatible_rights() {
        CommonRightsTest::<RoutingTestBuilderForAnalyzer>::new()
            .test_offer_from_component_manager_namespace_directory_incompatible_rights()
            .await
    }
}
