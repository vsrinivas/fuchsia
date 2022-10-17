// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::tests::utils::{self, AssertionOption, AssertionParameters, IqueryCommand, TestBuilder};
use assert_matches::assert_matches;
use iquery::types::Error;

// List command

#[fuchsia::test]
async fn test_list() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic-1")
        .await
        .add_basic_component("basic-2")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::List,
        golden_basename: "list_test",
        iquery_args: vec!["--accessor", "archivist:expose:fuchsia.diagnostics.ArchiveAccessor"],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn test_list_no_duplicates() {
    let test = TestBuilder::new().await.add_test_component("test").await.start().await;
    test.assert(AssertionParameters {
        command: IqueryCommand::List,
        golden_basename: "list_no_dups",
        iquery_args: vec!["--accessor", "archivist:expose:fuchsia.diagnostics.ArchiveAccessor"],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn test_list_filter_manifest() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::List,
        golden_basename: "list_filter_manifest",
        iquery_args: vec![
            "--manifest",
            "basic_component.cm",
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn test_list_with_urls() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::List,
        golden_basename: "list_with_url",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            "--with-url",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn list_archive() {
    let test = TestBuilder::new().await.add_basic_component("basic").await.start().await;
    test.assert(AssertionParameters {
        command: IqueryCommand::List,
        golden_basename: "list_archive",
        iquery_args: vec!["--accessor", "archivist:expose:fuchsia.diagnostics.ArchiveAccessor"],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

// List files command

#[fuchsia::test]
async fn list_files_basic() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::ListFiles,
        golden_basename: "list_files_basic",
        iquery_args: vec![&format!("./realm_builder:{}/basic", test.instance_child_name())],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn list_files_all() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::ListFiles,
        golden_basename: "list_files_all",
        iquery_args: vec![],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn log() {
    let test = TestBuilder::new().await.add_basic_component_with_logs("basic").await.start().await;

    test.assert(AssertionParameters {
        command: IqueryCommand::Logs,
        golden_basename: "log",
        iquery_args: vec!["--accessor", "archivist:expose:fuchsia.diagnostics.ArchiveAccessor"],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

// Selectors command

#[fuchsia::test]
async fn test_selectors_empty() {
    let result = utils::execute_command(&["selectors"]).await;
    assert_matches!(result, Err(Error::InvalidArguments(_)));
}

#[fuchsia::test]
async fn test_selectors() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic-1")
        .await
        .add_basic_component("basic-2")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    test.assert(AssertionParameters {
        command: IqueryCommand::Selectors,
        golden_basename: "selectors_test",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            &format!("{}/basic-1:root/fuchsia.inspect.Health", prefix),
            &format!("{}/basic-2:root", prefix),
            &format!("{}/test", prefix),
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn test_selectors_filter() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::Selectors,
        golden_basename: "selectors_filter_test",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            "--manifest",
            "basic_component.cm",
            "root/fuchsia.inspect.Health",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn selectors_archive() {
    let test = TestBuilder::new().await.add_basic_component("basic").await.start().await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    test.assert(AssertionParameters {
        command: IqueryCommand::Selectors,
        golden_basename: "selectors_archive",
        iquery_args: vec![
            &format!("{}/basic:root", prefix),
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

// Show

#[fuchsia::test]
async fn show_test() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic-1")
        .await
        .add_basic_component("basic-2")
        .await
        .add_basic_component("basic-3")
        .await
        .start()
        .await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    test.assert(AssertionParameters {
        command: IqueryCommand::Show,
        golden_basename: "show_test",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            &format!("{}/basic-1:root/fuchsia.inspect.Health", prefix),
            &format!("{}/basic-2:root:iquery", prefix),
            &format!("{}/basic-3", prefix),
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn show_test_with_files_basic() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic-1")
        .await
        .add_basic_component("basic-2")
        .await
        .add_basic_component("basic-3")
        .await
        .start()
        .await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    test.assert(AssertionParameters {
        command: IqueryCommand::Show,
        golden_basename: "show_test",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            "--file",
            "fuchsia.inspect.Tree",
            &format!("{}/basic-1:root/fuchsia.inspect.Health", prefix),
            &format!("{}/basic-2:root:iquery", prefix),
            &format!("{}/basic-3", prefix),
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn show_test_with_file_deprecated_inspect() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    test.assert(AssertionParameters {
        command: IqueryCommand::Show,
        golden_basename: "show_test_with_file_deprecated_inspect",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            "--file",
            "fuchsia.inspect.deprecated.Inspect",
            &format!("{}/test", &prefix),
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn show_test_with_file_root_inspect() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    test.assert(AssertionParameters {
        command: IqueryCommand::Show,
        golden_basename: "show_test_with_file_root_inspect",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            "--file",
            "root.inspect",
            &format!("{}/test", &prefix),
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn show_test_with_invalid_file() {
    TestBuilder::new()
        .await
        .add_basic_component("basic-1")
        .await
        .add_basic_component("basic-2")
        .await
        .add_basic_component("basic-3")
        .await
        .start()
        .await;

    let result = utils::execute_command(&[
        "show",
        "--accessor",
        "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
        "--file",
        "some.random.file",
    ])
    .await;
    assert_matches!(result, Ok(_));
}

#[fuchsia::test]
async fn empty_result_on_null_payload() {
    let test = TestBuilder::new().await.add_basic_component("basic").await.start().await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    let result =
        utils::execute_command(&["show", &format!("{}/basic:root/nothing:here", prefix)]).await;
    assert_matches!(result, Err(_));
}

#[fuchsia::test]
async fn show_component_does_not_exist() {
    let result = utils::execute_command(&[
        "show",
        "--accessor",
        "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
        "doesnt_exist",
    ])
    .await;
    assert_matches!(result, Ok(s) if s == "");
}

#[fuchsia::test]
async fn show_filter_manifest() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::Show,
        golden_basename: "show_filter_test",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            "--manifest",
            "basic_component.cm",
            "root/fuchsia.inspect.Health",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn show_filter_manifest_no_selectors() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::Show,
        golden_basename: "show_filter_no_selectors_test",
        iquery_args: vec![
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
            "--manifest",
            "basic_component.cm",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn show_archive() {
    let test = TestBuilder::new().await.add_basic_component("basic").await.start().await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    test.assert(AssertionParameters {
        command: IqueryCommand::Show,
        golden_basename: "show_archive",
        iquery_args: vec![
            &format!("{}/basic:root", prefix),
            "--accessor",
            "archivist:expose:fuchsia.diagnostics.ArchiveAccessor",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn list_accessors() {
    let test = TestBuilder::new().await.start().await;
    test.assert(AssertionParameters {
        command: IqueryCommand::ListAccessors,
        golden_basename: "list_accessors",
        iquery_args: vec![],
        opts: vec![],
    })
    .await;
}
