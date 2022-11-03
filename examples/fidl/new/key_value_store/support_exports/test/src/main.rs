// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{assert_logs_eq_to_golden, run_test, Client, Server, TestKind},
    fidl::prelude::*,
    fidl_examples_keyvaluestore_supportexports::StoreMarker,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

async fn test_export(
    test_name: &str,
    write_items: impl IntoIterator<Item = impl ToString>,
    max_export_size: u64,
) -> Result<(), Error> {
    let client = Client::new(test_name, "#meta/keyvaluestore_supportexports_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_supportexports_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder.set_config_value_string_vector(&client, "write_items", write_items).await?;
            builder.set_config_value_uint64(&client, "max_export_size", max_export_size).await?;
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
async fn test_export_success() -> Result<(), Error> {
    test_export("test_export_success", vec!["verse_1", "verse_2", "verse_3", "verse_4"], 10_000)
        .await
}

#[fasync::run_singlethreaded(test)]
async fn test_export_empty_error() -> Result<(), Error> {
    test_export("test_export_empty_error", Vec::<&str>::new(), 10_000).await
}

#[fasync::run_singlethreaded(test)]
async fn test_export_storage_too_small_error() -> Result<(), Error> {
    test_export(
        "test_export_storage_too_small_error",
        vec!["verse_1", "verse_2", "verse_3", "verse_4"],
        50,
    )
    .await
}
