// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_inspect_common::run_command,
    ffx_inspect_list_files_args::ListFilesCommand,
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    iquery::commands as iq,
};

#[ffx_plugin(
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn list_files(
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    #[ffx(machine = Vec<ListFilesResponseItem>)] writer: Writer,
    cmd: ListFilesCommand,
) -> Result<()> {
    run_command(rcs_proxy, diagnostics_proxy, iq::ListFilesCommand::from(cmd), writer).await
}

/// Test for `ffx inspect list-files`.
/// The test fixtures lives in `//src/diagnostics/iquery/test_support`.
#[cfg(test)]
mod test {
    use {
        super::*,
        assert_matches::assert_matches,
        ffx_inspect_test_utils::{setup_fake_diagnostics_bridge, setup_fake_rcs},
        ffx_writer::Format,
    };

    #[fuchsia::test]
    async fn test_list_files_no_parameters() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ListFilesCommand { monikers: vec![] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListFilesCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            iq::ListFilesResultItem::new(
                "example/component".to_owned(),
                vec!["fuchsia.inspect.Tree".to_owned()],
            ),
            iq::ListFilesResultItem::new(
                "other/component".to_owned(),
                vec!["fuchsia.inspect.Tree".to_owned()],
            ),
        ])
        .unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_list_files_with_valid_moniker() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ListFilesCommand { monikers: vec!["example/component".to_owned()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListFilesCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![iq::ListFilesResultItem::new(
            "example/component".to_owned(),
            vec!["fuchsia.inspect.Tree".to_owned()],
        )])
        .unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_list_files_with_invalid_moniker() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ListFilesCommand { monikers: vec!["bah".to_owned()] };
        let result = run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListFilesCommand::from(cmd),
            writer.clone(),
        )
        .await;

        assert_matches!(result, Err(_));
    }
}
