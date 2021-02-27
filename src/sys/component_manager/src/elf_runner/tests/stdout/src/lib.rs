// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_data::{Data, Logs, Severity},
    diagnostics_reader::{ArchiveReader, SubscriptionResultsStream},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2::ChildRef,
    fuchsia_async::{OnSignals, Task},
    fuchsia_zircon as zx,
    futures::StreamExt,
};

const EXPECTED_STDOUT_PAYLOAD: &str = "Hello Stdout!\n";
const EXPECTED_STDERR_PAYLOAD: &str = "Hello Stderr!\n";

struct Component {
    url: &'static str,
    moniker: &'static str,
}

const HELLO_WORLD_COMPONENTS: [Component; 2] = [
    Component {
        url: "fuchsia-pkg://fuchsia.com/elf_runner_stdout_test#meta/prints-when-launched-cpp.cm",
        moniker: "prints_when_launched_cpp",
    },
    Component {
        url: "fuchsia-pkg://fuchsia.com/elf_runner_stdout_test#meta/prints-when-launched-rust.cm",
        moniker: "prints_when_launched_rust",
    },
    /*
    // TODO(fxbug.dev/69588): Enable this when fix lands for Go runtime.
    Component {
        url: "fuchsia-pkg://fuchsia.com/elf_runner_stdout_test#meta/prints-when-launched-go.cm",
        moniker: "prints_when_launched_go",
    },
    */
];

// TODO(fxbug.dev/69684): Refactor this to receive puppet components
// through argv once ArchiveAccesor is exposed from Test Runner.
#[fuchsia::test]
async fn test_components_logs_to_stdout() {
    let realm = fuchsia_component::client::realm().unwrap();

    let mut subscription = launch_embedded_archivist().await;

    // Doing this in loop as opposed to separate test cases ensures linear
    // execution for Archivist's log streams.
    for component in HELLO_WORLD_COMPONENTS.iter() {
        let mut child_ref = ChildRef { name: component.moniker.to_string(), collection: None };

        // launch our child and wait for it to exit before asserting on its logs
        let (client_end, server_end) = create_endpoints::<DirectoryMarker>().unwrap();
        realm
            .bind_child(&mut child_ref, server_end)
            .await
            .expect("failed to make FIDL call")
            .expect("failed to bind child");
        OnSignals::new(&client_end, zx::Signals::CHANNEL_PEER_CLOSED).await.unwrap();

        // Puppets should emit 2 messages: one to stdout, another to stderr.
        let messages = subscription.by_ref().take(2).collect::<Vec<_>>().await;
        assert_all_have_attribution(&messages, &component);
        assert_any_has_content(&messages, EXPECTED_STDOUT_PAYLOAD, Severity::Info);
        assert_any_has_content(&messages, EXPECTED_STDERR_PAYLOAD, Severity::Warn);
    }
}

async fn launch_embedded_archivist() -> SubscriptionResultsStream<Logs> {
    let (subscription, mut errors) = ArchiveReader::new()
        .with_minimum_schema_count(0) // we want this to return even when no log messages
        .retry_if_empty(false)
        .snapshot_then_subscribe::<Logs>()
        .unwrap()
        .split_streams();

    let _ = Task::spawn(async move {
        if let Some(error) = errors.next().await {
            panic!("{:#?}", error);
        }
    });

    subscription
}

#[track_caller]
fn assert_all_have_attribution(messages: &[Data<Logs>], component: &Component) {
    let check_attribution = |msg: &Data<Logs>| {
        msg.moniker == component.moniker && msg.metadata.component_url == component.url
    };

    assert!(messages.iter().all(check_attribution));
}

#[track_caller]
fn assert_any_has_content(messages: &[Data<Logs>], payload: &str, severity: Severity) {
    let check_content = |msg: &Data<Logs>| {
        msg.metadata.severity == severity
            && msg.payload.as_ref().unwrap().get_property("message").unwrap().string().unwrap()
                == payload
    };

    assert!(messages.iter().any(check_content));
}
