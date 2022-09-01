// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_inspect_common::run_command,
    ffx_inspect_selectors_args::SelectorsCommand,
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    iquery::commands as iq,
};

#[ffx_plugin(
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn selectors(
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    #[ffx(machine = Vec<String>)] writer: Writer,
    cmd: SelectorsCommand,
) -> Result<()> {
    run_command(rcs_proxy, diagnostics_proxy, iq::SelectorsCommand::from(cmd), writer).await
}

#[cfg(test)]
mod test {
    use {
        super::*,
        errors::ResultExt as _,
        ffx_inspect_test_utils::{
            inspect_bridge_data, make_inspect_with_length, make_inspects_for_lifecycle,
            setup_fake_diagnostics_bridge, setup_fake_rcs, FakeBridgeData,
        },
        ffx_writer::Format,
        fidl_fuchsia_developer_remotecontrol::BridgeStreamParameters,
        fidl_fuchsia_diagnostics::{
            ClientSelectorConfiguration, DataType, SelectorArgument, StreamMode,
        },
        std::sync::Arc,
    };

    #[fuchsia::test]
    async fn test_selectors_no_parameters() {
        let params = BridgeStreamParameters {
            stream_mode: Some(StreamMode::Snapshot),
            data_type: Some(DataType::Inspect),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            ..BridgeStreamParameters::EMPTY
        };
        let expected_responses = Arc::new(vec![]);
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = SelectorsCommand { manifest: None, selectors: vec![], accessor_path: None };
        assert!(run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![FakeBridgeData::new(
                params,
                expected_responses.clone(),
            )]),
            iq::SelectorsCommand::from(cmd),
            writer.clone()
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());
    }

    #[fuchsia::test]
    async fn test_selectors_with_unknown_manifest() {
        let params = BridgeStreamParameters {
            stream_mode: Some(StreamMode::Snapshot),
            data_type: Some(DataType::Inspect),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            ..BridgeStreamParameters::EMPTY
        };
        let expected_responses = Arc::new(vec![]);
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = SelectorsCommand {
            manifest: Some(String::from("some-bad-moniker")),
            selectors: vec![],
            accessor_path: None,
        };
        assert!(run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![FakeBridgeData::new(
                params,
                expected_responses.clone(),
            )]),
            iq::SelectorsCommand::from(cmd),
            writer.clone()
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());
    }

    #[fuchsia::test]
    async fn test_selectors_with_manifest_that_exists() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = SelectorsCommand {
            manifest: Some(String::from("moniker1")),
            selectors: vec![],
            accessor_path: None,
        };
        let lifecycle_data = inspect_bridge_data(
            ClientSelectorConfiguration::SelectAll(true),
            make_inspects_for_lifecycle(),
        );
        let inspects = vec![
            make_inspect_with_length(String::from("test/moniker1"), 1, 20),
            make_inspect_with_length(String::from("test/moniker1"), 3, 10),
            make_inspect_with_length(String::from("test/moniker1"), 6, 30),
        ];
        let inspect_data = inspect_bridge_data(
            ClientSelectorConfiguration::Selectors(vec![SelectorArgument::RawSelector(
                String::from("test/moniker1:root"),
            )]),
            inspects,
        );
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![lifecycle_data, inspect_data]),
            iq::SelectorsCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            String::from("test/moniker1:name:hello_1"),
            String::from("test/moniker1:name:hello_3"),
            String::from("test/moniker1:name:hello_6"),
        ])
        .unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_selectors_with_selectors() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = SelectorsCommand {
            manifest: None,
            selectors: vec![String::from("test/moniker1:name:hello_3")],
            accessor_path: None,
        };
        let lifecycle_data = inspect_bridge_data(
            ClientSelectorConfiguration::SelectAll(true),
            make_inspects_for_lifecycle(),
        );
        let inspects = vec![make_inspect_with_length(String::from("test/moniker1"), 3, 10)];
        let inspect_data = inspect_bridge_data(
            ClientSelectorConfiguration::Selectors(vec![SelectorArgument::RawSelector(
                String::from("test/moniker1:name:hello_3"),
            )]),
            inspects,
        );
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![lifecycle_data, inspect_data]),
            iq::SelectorsCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        let expected =
            serde_json::to_string(&vec![String::from("test/moniker1:name:hello_3")]).unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }
}
