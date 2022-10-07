// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{assert_logs_eq_to_golden, run_test, Client, Server, TestKind},
    fidl::prelude::*,
    fidl_examples_keyvaluestore_addreaditem::StoreMarker,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

#[fasync::run_singlethreaded(test)]
async fn test_read_item_success() -> Result<(), Error> {
    let test_name = "test_read_item_success";
    let client = Client::new(test_name, "#meta/keyvaluestore_addreaditem_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_addreaditem_server.cm");
    let key = "verse_1";

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_from_package(&client).await?;
            builder
                .set_config_value_string_vector(&client, "read_items", [key.to_string()])
                .await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            assert_logs_eq_to_golden(&raw_logs, &client);
            assert_logs_eq_to_golden(&raw_logs, &server);
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_read_item_error() -> Result<(), Error> {
    let test_name = "test_read_item_error";
    let client = Client::new(test_name, "#meta/keyvaluestore_addreaditem_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_addreaditem_server.cm");
    let key = "verse_1000";

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_from_package(&client).await?;
            builder
                .set_config_value_string_vector(&client, "read_items", [key.to_string()])
                .await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            assert_logs_eq_to_golden(&raw_logs, &client);
            assert_logs_eq_to_golden(&raw_logs, &server);
        },
    )
    .await
}
