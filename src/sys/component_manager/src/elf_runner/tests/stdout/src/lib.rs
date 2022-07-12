// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_data::{Data, Logs, Severity},
    diagnostics_reader::ArchiveReader,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io::DirectoryMarker,
    fuchsia_async::{Duration, TestExecutor, Timer},
    fuchsia_component::client,
    test_case::test_case,
};

const COLLECTION_NAME: &str = "puppets";

/// Used to parameterize test cases by which log messages to expect.
enum Expected {
    Stdout,
    Stderr,
    Both,
}

impl Expected {
    fn len(&self) -> usize {
        self.stdout() as usize + self.stderr() as usize
    }

    fn stdout(&self) -> bool {
        matches!(self, Expected::Stdout | Expected::Both)
    }

    fn stderr(&self) -> bool {
        matches!(self, Expected::Stderr | Expected::Both)
    }
}

#[test_case("#meta/logs-stdout-and-stderr-cpp.cm", "logs_stdout_and_stderr_cpp", Expected::Both; "cpp logs to both")]
#[test_case("#meta/logs-default-cpp.cm", "logs_default_cpp", Expected::Both; "cpp logs to both implicitly by default")]
#[test_case("#meta/logs-stdout-cpp.cm", "logs_stdout_cpp", Expected::Stdout; "cpp logs to stdout")]
#[test_case("#meta/logs-stderr-cpp.cm", "logs_stderr_cpp", Expected::Stderr; "cpp logs to stderr")]
#[test_case("#meta/logs-stdout-and-stderr-go.cm", "logs_stdout_and_stderr_go", Expected::Both; "go logs to both")]
#[test_case("#meta/logs-default-go.cm", "logs_default_go", Expected::Both; "go logs to both implicitly by default")]
#[test_case("#meta/logs-stdout-go.cm", "logs_stdout_go", Expected::Stdout; "go logs to stdout")]
#[test_case("#meta/logs-stderr-go.cm", "logs_stderr_go", Expected::Stderr; "go logs to stderr")]
#[test_case("#meta/logs-stdout-and-stderr-rust.cm", "logs_stdout_and_stderr_rust", Expected::Both; "rust logs to both")]
#[test_case("#meta/logs-default-rust.cm", "logs_default_rust", Expected::Both; "rust logs to both implicitly by default")]
#[test_case("#meta/logs-stdout-rust.cm", "logs_stdout_rust", Expected::Stdout; "rust logs to stdout")]
#[test_case("#meta/logs-stderr-rust.cm", "logs_stderr_rust", Expected::Stderr; "rust logs to stderr")]
#[fuchsia::test(add_test_attr = false)]
fn launch_component_and_check_messages(url: &str, moniker: &str, expected_messages: Expected) {
    // TODO(https://fxbug.dev/94784) inline `test_inner` once the fuchsia test macro supports it
    TestExecutor::new().unwrap().run_singlethreaded(test_inner(url, moniker, expected_messages));
}

async fn test_inner(url: &str, moniker: &str, expected: Expected) {
    // start the child in our collection
    let realm = client::realm().expect("failed to connect to fuchsia.component.Realm");

    let mut collection_ref = fdecl::CollectionRef { name: COLLECTION_NAME.to_owned() };
    let child_decl = fdecl::Child {
        name: Some(moniker.to_owned()),
        url: Some(url.to_owned()),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };
    realm
        .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
        .await
        .expect("Failed to make FIDL call")
        .expect("Failed to create child");
    let mut child_ref =
        fdecl::ChildRef { name: moniker.to_owned(), collection: Some(COLLECTION_NAME.to_owned()) };
    let (exposed_dir, server_end) = create_proxy::<DirectoryMarker>().unwrap();
    realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .expect("failed to make FIDL call")
        .expect("failed to open exposed dir of child");
    client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir).unwrap();
    let full_moniker = &format!("{}:{}", COLLECTION_NAME, moniker);

    // wait a little to increase chances we detect extra messages we don't want
    Timer::new(Duration::from_seconds(3)).await;

    // Golang prints messages to stdout and stderr when it finds it's missing any of the stdio
    // handles. Ignore messages that come from the runtime so we can match on our expectations.
    // TODO(fxbug.dev/69588): Remove this workaround.
    let num_expected = if moniker.ends_with("go") {
        // wait for the expected number of additional messages from the go runtime
        expected.len() + if expected.stderr() { 2 } else { 1 }
    } else {
        expected.len()
    };
    // read log messages
    let mut messages = ArchiveReader::new()
        .select_all_for_moniker(full_moniker) // only return logs for this puppet
        .with_minimum_schema_count(num_expected) // retry until we have the expected number
        .snapshot::<Logs>()
        .await
        .unwrap()
        .into_iter()
        .filter(|log| {
            // TODO(fxbug.dev/69588): Remove this workaround.
            !log.msg()
                .map(|m| m.starts_with("runtime") || m.starts_with("syscall"))
                .unwrap_or_default()
        })
        .collect::<Vec<_>>();
    // stdout and stderr are not ordered w.r.t. each other, pick a stable order for assertions
    messages.sort_by_key(|l| l.metadata.severity);
    let mut messages = messages.into_iter();

    // assert on the log messages we're reading
    if expected.stdout() {
        let observed = messages.next().unwrap();
        assert_eq!(get_msg(&observed), "Hello Stdout!");
        assert_eq!(observed.metadata.severity, Severity::Info);
    }
    if expected.stderr() {
        let observed = messages.next().unwrap();
        assert_eq!(get_msg(&observed), "Hello Stderr!");
        assert_eq!(observed.metadata.severity, Severity::Warn);
    }
    assert_eq!(messages.next(), None, "no more messages expected from child");
}

fn get_msg(m: &Data<Logs>) -> String {
    m.payload_message().unwrap().get_property("value").unwrap().string().unwrap().to_string()
}
