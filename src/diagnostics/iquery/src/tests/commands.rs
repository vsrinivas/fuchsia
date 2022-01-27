// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::tests::utils::{self, AssertionOption, AssertionParameters, IqueryCommand, TestBuilder};
use assert_matches::assert_matches;
use iquery::types::Error;
use std::path::Path;
use tempfile::tempdir;

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
        iquery_args: vec![],
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
        iquery_args: vec![],
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
        iquery_args: vec!["--manifest", "basic_component.cm"],
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
        iquery_args: vec!["--with-url"],
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
        iquery_args: vec!["--accessor-path", "/svc/fuchsia.diagnostics.ArchiveAccessor"],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

// List files command

#[fuchsia::test]
async fn list_files_empty_path_uses_cwd() {
    let test = TestBuilder::new().await.add_basic_component("basic").await.start().await;
    std::env::set_current_dir(Path::new("/hub")).expect("change dir");
    test.assert(AssertionParameters {
        command: IqueryCommand::ListFiles,
        golden_basename: "list_files_cwd",
        iquery_args: vec![&format!("children/realm_builder:{}/", test.instance_child_name())],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn list_files() {
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
        golden_basename: "list_files_test",
        iquery_args: vec![&format!(
            "/hub/children/realm_builder:{}/children/test/",
            test.instance_child_name()
        )],
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
        iquery_args: vec![],
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
        iquery_args: vec!["--manifest", "basic_component.cm", "root/fuchsia.inspect.Health"],
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
            "--accessor-path",
            "/svc/fuchsia.diagnostics.ArchiveAccessor",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

// Show file

#[fuchsia::test]
async fn test_no_paths() {
    let result = utils::execute_command(&["show-file"]).await;
    assert_matches!(result, Err(Error::InvalidArguments(_)));
}

#[fuchsia::test]
async fn test_invalid_location() {
    let dir = tempdir().unwrap();
    let file_path = dir.path().join("root.inspect").to_string_lossy().to_string();
    let result = utils::execute_command(&["show-file", &file_path]).await;
    assert_matches!(result, Err(Error::ReadLocation(path, _)) if path == file_path);
}

#[fuchsia::test]
async fn show_file_test() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic")
        .await
        .add_test_component("test")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::ShowFile,
        golden_basename: "show_file_test",
        iquery_args: vec![
            &format!("/hub/children/realm_builder:{}/children/basic/exec/out/diagnostics/fuchsia.inspect.Tree",
                test.instance_child_name()),
            &format!("/hub/children/realm_builder:{}/children/test/exec/out/diagnostics/*",
                test.instance_child_name()),
        ],
        opts: vec![AssertionOption::Retry],
    }).await;
}

#[fuchsia::test]
async fn inspect_vmo_file_directly() {
    let test = TestBuilder::new().await.add_test_component("test").await.start().await;
    test.assert(AssertionParameters {
        command: IqueryCommand::ShowFile,
        golden_basename: "show_file_vmo",
        iquery_args: vec![&format!(
            "/hub/children/realm_builder:{}/children/test/exec/out/diagnostics/root.inspect",
            test.instance_child_name()
        )],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

// Show

#[fuchsia::test]
async fn test_no_selectors() {
    let test = TestBuilder::new()
        .await
        .add_basic_component("basic-1")
        .await
        .add_basic_component("basic-2")
        .await
        .start()
        .await;
    test.assert(AssertionParameters {
        command: IqueryCommand::Show,
        golden_basename: "show_all_test",
        iquery_args: vec![],
        opts: vec![AssertionOption::Retry, AssertionOption::RemoveArchivist],
    })
    .await;
}

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
            &format!("{}/basic-1:root/fuchsia.inspect.Health", prefix),
            &format!("{}/basic-2:root:iquery", prefix),
            &format!("{}/basic-3", prefix),
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn empty_result_on_null_payload() {
    let test = TestBuilder::new().await.add_basic_component("basic").await.start().await;
    let prefix = format!("realm_builder\\:{}", test.instance_child_name());
    let result =
        utils::execute_command(&["show", &format!("{}/basic:root/nothing:here", prefix)]).await;
    assert_matches!(result, Ok(res) if res == "" || res.contains("payload: null"));
}

#[fuchsia::test]
async fn show_component_doesnt_exist() {
    let result = utils::execute_command(&["show", "doesnt_exist"]).await;
    assert_matches!(result, Ok(res) if res == "");
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
        iquery_args: vec!["--manifest", "basic_component.cm", "root/fuchsia.inspect.Health"],
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
        iquery_args: vec!["--manifest", "basic_component.cm"],
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
            "--accessor-path",
            "/svc/fuchsia.diagnostics.ArchiveAccessor",
        ],
        opts: vec![AssertionOption::Retry],
    })
    .await;
}

#[fuchsia::test]
async fn list_accessors() {
    let test = TestBuilder::new().await.start().await;
    std::env::set_current_dir(Path::new(&format!("/svc"))).expect("change dir");
    test.assert(AssertionParameters {
        command: IqueryCommand::ListAccessors,
        golden_basename: "list_accessors",
        iquery_args: vec![],
        opts: vec![],
    })
    .await;
}
