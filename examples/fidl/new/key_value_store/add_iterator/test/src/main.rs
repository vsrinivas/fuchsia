// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{assert_logs_eq_to_golden, run_test, Client, Server, TestKind},
    fidl::prelude::*,
    fidl_examples_keyvaluestore_additerator::StoreMarker,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

async fn test_iteration(test_name: &str, iterate_from: &str) -> Result<(), Error> {
    let client = Client::new(test_name, "#meta/keyvaluestore_additerator_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_additerator_server.cm");

    run_test(
        StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder
                .set_config_value_string_vector(
                    &client,
                    "write_items",
                    vec!["verse_1", "verse_2", "verse_3", "verse_4"],
                )
                .await?;
            builder.set_config_value_string(&client, "iterate_from", iterate_from).await?;
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
async fn test_iteration_success() -> Result<(), Error> {
    test_iteration("test_iteration_success", "verse_2").await
}

#[fasync::run_singlethreaded(test)]
async fn test_iteration_error() -> Result<(), Error> {
    test_iteration("test_iteration_error", "does_not_exist").await
}
