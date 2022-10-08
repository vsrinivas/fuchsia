// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_data::{Data, Logs},
    example_tester::{assert_logs_eq_to_golden, run_test, Client, Server, TestKind},
    fidl::prelude::*,
    fidl_examples_canvas::InstanceMarker,
    fuchsia_async as fasync,
    fuchsia_component_test::{ChildRef, RealmBuilder},
};

#[fasync::run_singlethreaded(test)]
async fn test_draw_success() -> Result<(), Error> {
    let test_name = "test_draw_success";
    let client = Client::new(test_name, "#meta/canvas_client.cm");
    let server = Server::new(test_name, "#meta/canvas_server.cm");

    run_test(
        InstanceMarker::PROTOCOL_NAME,
        TestKind::ClientAndServer { client: &client, server: &server },
        |builder: RealmBuilder, client: ChildRef| async move {
            builder.init_mutable_config_to_empty(&client).await?;
            builder
                .set_config_value_string_vector(
                    &client,
                    "script",
                    ["-5,0:0,0", "-2,2:5,6", "4,-3:-2,-2", "WAIT", "6,-1:7,0", "7,-8:-4,3", "WAIT"],
                )
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
