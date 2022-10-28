// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{assert_logs_eq_to_golden, run_test, Client, Server, TestKind},
    fidl::prelude::*,
    fidl_examples_keyvaluestore_supporttrees::StoreMarker,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

#[fasync::run_singlethreaded(test)]
async fn test_write_success() -> Result<(), Error> {
    let test_name = "test_write_success";
    let client = Client::new(test_name, "#meta/keyvaluestore_supporttrees_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_supporttrees_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder
                .set_config_value_string_vector(&client, "write_items", ["verse_1", "verse_2"])
                .await?;
            builder
                .set_config_value_string_vector(
                    &client,
                    "write_nested",
                    ["rest_of_poem\nverse_3\nverse_4"],
                )
                .await?;
            builder.set_config_value_string_vector(&client, "write_null", ["null_verse"]).await?;
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
async fn test_empty_nested() -> Result<(), Error> {
    let test_name = "test_empty_nested";
    let client = Client::new(test_name, "#meta/keyvaluestore_supporttrees_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_supporttrees_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder
                .set_config_value_string_vector(&client, "write_items", Vec::<&str>::new())
                .await?;
            builder
                .set_config_value_string_vector(&client, "write_nested", ["invalid_empty"])
                .await?;
            builder
                .set_config_value_string_vector(&client, "write_null", Vec::<&str>::new())
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
