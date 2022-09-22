// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{logs_to_str, run_test, Client, Server, TestKind},
    fidl::prelude::*,
    fidl_examples_keyvaluestore_addreaditem::ReadError,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

#[fasync::run_singlethreaded(test)]
async fn test_read_item_success() -> Result<(), Error> {
    let test_name = "test_write_item_success";
    let client = Client::new(test_name, "#meta/keyvaluestore_addreaditem_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_addreaditem_server.cm");
    let key = "verse_1";

    run_test(
        fidl_examples_keyvaluestore_addreaditem::StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_from_package(&client).await?;
            builder
                .set_config_value_string_vector(&client, "read_items", [key.to_string()])
                .await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            // Both components have started successfully.
            let all_logs = logs_to_str(&raw_logs, None);
            assert_eq!(all_logs.filter(|log| *log == "Started").count(), 2);

            // Ensure that the server received the read request, logged it, and responded
            // successfully.
            let server_logs = logs_to_str(&raw_logs, Some(vec![&server])).collect::<Vec<&str>>();
            assert_eq!(
                server_logs
                    .clone()
                    .into_iter()
                    .find(|log| *log == "ReadItem request received")
                    .is_some(),
                true
            );
            assert_eq!(
                server_logs.into_iter().find(|log| *log == "ReadItem response sent").is_some(),
                true
            );

            // Ensure that the client received the correct reply to the `ReadItem` method call.
            let path = format!("/pkg/data/{}.txt", key);
            let want_value = std::fs::read_to_string(path).unwrap();
            let want_value_lines = want_value.lines().count();
            let client_logs = logs_to_str(&raw_logs, Some(vec![&client])).collect::<Vec<&str>>();
            let last_client_logs = client_logs
                .into_iter()
                .rev()
                .take(want_value_lines)
                .rev()
                .map(|line| line.to_string())
                .collect::<Vec<String>>()
                .join("\n");
            assert_eq!(
                last_client_logs,
                format!(
                    "ReadItem Success: key: {}, value: {}",
                    key,
                    want_value.strip_suffix("\n").unwrap()
                )
            );
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_read_item_err() -> Result<(), Error> {
    let test_name = "test_write_item_success";
    let client = Client::new(test_name, "#meta/keyvaluestore_addreaditem_client.cm");
    let server = Server::new(test_name, "#meta/keyvaluestore_addreaditem_server.cm");
    let key = "verse_1000";

    run_test(
        fidl_examples_keyvaluestore_addreaditem::StoreMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_from_package(&client).await?;
            builder
                .set_config_value_string_vector(&client, "read_items", [key.to_string()])
                .await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            // Both components have started successfully.
            let all_logs = logs_to_str(&raw_logs, None);
            assert_eq!(all_logs.filter(|log| *log == "Started").count(), 2);

            // Ensure that the server received the read request, logged it, and responded
            // successfully.
            let server_logs = logs_to_str(&raw_logs, Some(vec![&server])).collect::<Vec<&str>>();
            assert_eq!(
                server_logs
                    .clone()
                    .into_iter()
                    .find(|log| *log == "ReadItem request received")
                    .is_some(),
                true
            );
            assert_eq!(
                server_logs.into_iter().find(|log| *log == "ReadItem response sent").is_some(),
                true
            );

            // Test that the verse, which does not exist, was not found.
            let client_logs = logs_to_str(&raw_logs, Some(vec![&client]));
            assert_eq!(
                client_logs.last().expect("no response"),
                format!("ReadItem Error: {}", ReadError::NotFound.into_primitive())
            );
        },
    )
    .await
}
