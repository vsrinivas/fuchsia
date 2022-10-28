// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{assert_logs_eq_to_golden, run_test, Client, Server, TestKind},
    fidl::prelude::*,
    fidl_examples_keyvaluestore_baseline::StoreMarker,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

#[fasync::run_singlethreaded(test)]
async fn test_write_item_success() -> Result<(), Error> {
    let test_name = "test_write_item_success";
    let client = Client::new(test_name, "#meta/keyvaluestore_baseline_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_baseline_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder.set_config_value_string_vector(&client, "write_items", ["verse_1"]).await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            assert_logs_eq_to_golden(&raw_logs, &client);
            assert_logs_eq_to_golden(&raw_logs, &server);
        },
    )
    .await
}

async fn test_write_item_invalid(test_name: &str, input: &str) -> Result<(), Error> {
    let client = Client::new(test_name, "#meta/keyvaluestore_baseline_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_baseline_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder.set_config_value_string_vector(&client, "write_items", [input]).await?;
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
async fn test_write_item_error_invalid_key() -> Result<(), Error> {
    test_write_item_invalid(
        "test_write_item_error_invalid_key",
        // A trailing underscore makes for an invalid key per the rules in
        // keyvaluestore.test.fidl, hence the odd name here.
        "error_invalid_key_",
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_write_item_error_invalid_value() -> Result<(), Error> {
    test_write_item_invalid("test_write_item_error_invalid_value", "error_invalid_value").await
}

#[fasync::run_singlethreaded(test)]
async fn test_write_item_error_already_found() -> Result<(), Error> {
    let test_name = "test_write_item_error_already_found";
    let client = Client::new(test_name, "#meta/keyvaluestore_baseline_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_baseline_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder
                .set_config_value_string_vector(&client, "write_items", ["verse_1", "verse_1"])
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
