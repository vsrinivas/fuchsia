// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{assert_logs_eq_to_golden, run_test, Client, Server, TestKind},
    fidl::prelude::*,
    fidl_examples_keyvaluestore_usegenericvalues::StoreMarker,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

async fn test_write_items_twice(
    test_name: &str,
    set_overwrite: bool,
    set_concat: bool,
) -> Result<(), Error> {
    let client = Client::new(test_name, "#meta/keyvaluestore_usegenericvalues_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_usegenericvalues_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder.set_config_value_bool(&client, "set_overwrite_option", set_overwrite).await?;
            builder.set_config_value_bool(&client, "set_concat_option", set_concat).await?;
            builder.set_config_value_string_vector(&client, "write_bytes", ["foo", "bar"]).await?;
            builder
                .set_config_value_string_vector(&client, "write_strings", ["baz", "qux"])
                .await?;
            builder.set_config_value_uint64_vector(&client, "write_uint64s", [1, 101]).await?;
            builder.set_config_value_int64_vector(&client, "write_int64s", [-2, -102]).await?;
            builder.set_config_value_uint64_vector(&client, "write_uint128s", [3, 103]).await?;
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
async fn test_write_items_overwrite_success() -> Result<(), Error> {
    test_write_items_twice("test_write_items_overwrite_success", true, false).await
}

#[fasync::run_singlethreaded(test)]
async fn test_write_items_concat_success() -> Result<(), Error> {
    test_write_items_twice("test_write_items_concat_success", false, true).await
}

#[fasync::run_singlethreaded(test)]
async fn test_write_items_default_error() -> Result<(), Error> {
    test_write_items_twice("test_write_items_default_error", false, false).await
}

#[fasync::run_singlethreaded(test)]
async fn test_write_items_default_success() -> Result<(), Error> {
    let test_name = "test_write_items_default_success";
    let client = Client::new(test_name, "#meta/keyvaluestore_usegenericvalues_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_usegenericvalues_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder.set_config_value_bool(&client, "set_overwrite_option", false).await?;
            builder.set_config_value_bool(&client, "set_concat_option", false).await?;
            builder.set_config_value_string_vector(&client, "write_bytes", ["foo"]).await?;
            builder.set_config_value_string_vector(&client, "write_strings", ["baz"]).await?;
            builder.set_config_value_uint64_vector(&client, "write_uint64s", [1]).await?;
            builder.set_config_value_int64_vector(&client, "write_int64s", [-2]).await?;
            builder.set_config_value_uint64_vector(&client, "write_uint128s", [3]).await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            assert_logs_eq_to_golden(&raw_logs, &client);
            assert_logs_eq_to_golden(&raw_logs, &server);
        },
    )
    .await
}
