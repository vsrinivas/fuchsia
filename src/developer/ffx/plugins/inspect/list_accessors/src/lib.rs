// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_inspect_common::run_command,
    ffx_inspect_list_accessors_args::ListAccessorsCommand,
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    iquery::commands as iq,
};

#[ffx_plugin(
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn list_accessors(
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    #[ffx(machine = Vec<String>)] writer: Writer,
    cmd: ListAccessorsCommand,
) -> Result<()> {
    run_command(rcs_proxy, diagnostics_proxy, iq::ListAccessorsCommand::from(cmd), writer).await
}

/// Test for `ffx inspect list-accessors`.
/// The test fixtures lives in `//src/diagnostics/iquery/test_support`.
#[cfg(test)]
mod test {
    use {
        super::*,
        ffx_inspect_test_utils::{setup_fake_diagnostics_bridge, setup_fake_rcs},
        ffx_writer::Format,
    };

    #[fuchsia::test]
    async fn test_list_accessors_no_parameters() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ListAccessorsCommand { paths: vec![] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            String::from("example/component:expose:fuchsia.diagnostics.ArchiveAccessor"),
            String::from("other/component:out:fuchsia.diagnostics.MagicArchiveAccessor"),
            String::from("foo/component:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
            String::from("foo/bar/thing:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
        ])
        .unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_list_accessors_subcomponent() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ListAccessorsCommand { paths: vec!["foo".into()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            String::from("foo/component:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
            String::from("foo/bar/thing:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
        ])
        .unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_list_accessors_deeper_subdirectory() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ListAccessorsCommand { paths: vec!["foo/bar".into()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![String::from(
            "foo/bar/thing:expose:fuchsia.diagnostics.FeedbackArchiveAccessor",
        )])
        .unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_list_accessors_multiple_paths() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ListAccessorsCommand { paths: vec!["example".into(), "foo/bar/".into()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            String::from("example/component:expose:fuchsia.diagnostics.ArchiveAccessor"),
            String::from("foo/bar/thing:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
        ])
        .unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_list_accessors_path_with_no_accessors() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ListAccessorsCommand { paths: vec!["this/is/bad".into()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = "[]".to_owned();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }
}
