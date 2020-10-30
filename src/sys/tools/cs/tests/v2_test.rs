// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cs::v2::V2Component,
    test_utils_lib::{events::*, matcher::EventMatcher, opaque_test::*},
};

async fn launch_cs(hub_v2_path: String) -> Vec<String> {
    // Combine the tree and detailed output for comparison purposes.
    let root = V2Component::new_root_component(hub_v2_path).await;
    let mut lines = root.generate_tree();
    lines.push("".to_string());
    lines.append(&mut root.generate_details(""));
    lines
}

fn compare_output(actual: Vec<String>, expected: Vec<&str>) {
    // Print out the outputs in a readable format
    let print_actual = actual.join("\n");
    let print_actual = format!("------------ ACTUAL OUTPUT ----------------\n{}\n-------------------------------------------", print_actual);
    let print_expected = expected.join("\n");
    let print_expected = format!("------------ EXPECT OUTPUT ----------------\n{}\n-------------------------------------------", print_expected);
    println!("{}\n{}", print_actual, print_expected);

    // Now compare
    let actual: Vec<&str> = actual.iter().map(|line| line.as_str()).collect();
    assert_eq!(actual, expected);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn empty_component() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/cs-tests#meta/empty.cm").await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

    event_source.start_component_tree().await;

    // Root must be created first
    let event = EventMatcher::ok().moniker(".").expect_match::<Started>(&mut event_stream).await;
    event.resume().await.unwrap();

    let hub_v2_path = test.get_hub_v2_path().into_os_string().into_string().unwrap();
    let actual = launch_cs(hub_v2_path).await;
    compare_output(
        actual,
        vec![
            "<root>",
            "",
            "<root>:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/empty.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
        ],
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn tree() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/cs-tests#meta/root.cm").await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

    event_source.start_component_tree().await;

    // Root must be created first
    let event = EventMatcher::ok().moniker(".").expect_match::<Started>(&mut event_stream).await;
    event.resume().await.unwrap();

    // 6 descendants are created eagerly. Order is irrelevant.
    for _ in 1..=6 {
        let event = EventMatcher::ok().expect_match::<Started>(&mut event_stream).await;
        event.resume().await.unwrap();
    }

    let hub_v2_path = test.get_hub_v2_path().into_os_string().into_string().unwrap();
    let actual = launch_cs(hub_v2_path).await;
    compare_output(
        actual,
        vec![
            "<root>",
            "  bar",
            "    baz",
            "  foo",
            "    bar",
            "      baz",
            "    baz",
            "",
            "<root>:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/root.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
            "<root>:0/bar:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/bar.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
            "<root>:0/bar:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/baz.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
            "<root>:0/foo:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/foo.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
            "<root>:0/foo:0/bar:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/bar.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
            "<root>:0/foo:0/bar:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/baz.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
            "<root>:0/foo:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/baz.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
        ],
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn echo_realm() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/cs-tests#meta/echo_realm.cm").await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();

    {
        let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();
        event_source.start_component_tree().await;

        // 3 components are started. Order is irrelevant.
        // root and echo_client are started eagerly.
        // echo_server is started after echo_client connects to the Echo service.
        for _ in 1..=3 {
            let event = EventMatcher::ok().expect_match::<Started>(&mut event_stream).await;
            event.resume().await.unwrap();
        }
    }

    let hub_v2_path = test.get_hub_v2_path().into_os_string().into_string().unwrap();
    let actual = launch_cs(hub_v2_path).await;
    compare_output(
        actual,
        vec![
            "<root>",
            "  echo_server",
            "  indef_echo_client",
            "",
            "<root>:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/echo_realm.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "",
            "<root>:0/echo_server:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/echo_server.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (2)",
            "  - fidl.examples.routing.echo.Echo",
            "  - hub",
            "- Incoming Services (0)",
            "- Outgoing Services (1)",
            "  - fidl.examples.routing.echo.Echo",
            "",
            "<root>:0/indef_echo_client:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs-tests#meta/indef_echo_client.cm",
            "- Type: v2 static component",
            "- Exposed Capabilities (0)",
            "- Incoming Services (2)",
            "  - fidl.examples.routing.echo.Echo",
            "  - fuchsia.logger.LogSink",
            "- Outgoing Services (0)",
            "",
        ],
    );
}
