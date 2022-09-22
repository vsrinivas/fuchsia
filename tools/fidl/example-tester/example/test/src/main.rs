// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{logs_to_str, run_test, Client, Proxy, Server, TestKind},
    fidl::prelude::*,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

// Tests the framework for a single component running locally. This is useful for testing things
// like local logging and persistent FIDL.
#[fasync::run_singlethreaded(test)]
async fn test_one_component() -> Result<(), Error> {
    let client = Client::new("test_one_component", "#meta/example_tester_example_client.cm");

    run_test(
        fidl_test_exampletester::SimpleMarker::PROTOCOL_NAME,
        TestKind::StandaloneComponent { client: &client },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder.set_config_value_bool(&client, "do_in_process", true).await?;
            builder.set_config_value_uint8(&client, "augend", 1).await?;
            builder.set_config_value_uint8(&client, "addend", 3).await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            let all_logs = logs_to_str(&raw_logs, None);
            assert_eq!(all_logs.filter(|log| *log == "Started").count(), 1);

            let client_logs = logs_to_str(&raw_logs, Some(vec![&client]));
            assert_eq!(client_logs.last().expect("no response"), format!("Response: {}", 4));
        },
    )
    .await
}

// Tests the standard FIDL IPC scenario, with a client talking to a server.
#[fasync::run_singlethreaded(test)]
async fn test_two_component() -> Result<(), Error> {
    let augend = 1;
    let addend = 2;
    let want_response = 3;
    let test_name = "test_two_component";
    let client = Client::new(test_name.clone(), "#meta/example_tester_example_client.cm");
    let server = Server::new(test_name, "#meta/example_tester_example_server.cm");

    run_test(
        fidl_test_exampletester::SimpleMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder.set_config_value_bool(&client, "do_in_process", false).await?;
            builder.set_config_value_uint8(&client, "augend", augend).await?;
            builder.set_config_value_uint8(&client, "addend", addend).await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            let all_logs = logs_to_str(&raw_logs, None);
            assert_eq!(all_logs.filter(|log| *log == "Started").count(), 2);

            let non_client_logs = logs_to_str(&raw_logs, Some(vec![&server]));
            assert_eq!(
                non_client_logs
                    .filter(|log| *log == "Request received" || *log == "Response sent")
                    .count(),
                2
            );

            let client_logs = logs_to_str(&raw_logs, Some(vec![&client]));
            assert_eq!(
                client_logs.last().expect("no response"),
                format!("Response: {}", want_response)
            );
        },
    )
    .await
}

// Tests a client-server IPC interaction mediated by a proxy in the middle.
#[fasync::run_singlethreaded(test)]
async fn test_three_component() -> Result<(), Error> {
    let augend = 4;
    let addend = 5;
    let want_response = 9;
    let test_name = "test_three_component";
    let client = Client::new(test_name.clone(), "#meta/example_tester_example_client.cm");
    let proxy = Proxy::new(test_name.clone(), "#meta/example_tester_example_proxy.cm");
    let server = Server::new(test_name, "#meta/example_tester_example_server.cm");

    run_test(
        fidl_test_exampletester::SimpleMarker::PROTOCOL_NAME,
        TestKind::ClientProxyAndServer { client: &client, proxy: &proxy, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder.set_config_value_bool(&client, "do_in_process", false).await?;
            builder.set_config_value_uint8(&client, "augend", augend).await?;
            builder.set_config_value_uint8(&client, "addend", addend).await?;
            Ok::<(RealmBuilder, ChildRef), Error>((builder, client))
        },
        |raw_logs: Vec<Data<Logs>>| {
            let all_logs = logs_to_str(&raw_logs, None);
            assert_eq!(all_logs.filter(|log| *log == "Started").count(), 3);

            let non_client_logs = logs_to_str(&raw_logs, Some(vec![&proxy, &server]));
            assert_eq!(
                non_client_logs
                    .filter(|log| *log == "Request received" || *log == "Response sent")
                    .count(),
                4
            );

            let client_logs = logs_to_str(&raw_logs, Some(vec![&client]));
            assert_eq!(
                client_logs.last().expect("no response"),
                format!("Response: {}", want_response)
            );
        },
    )
    .await
}
