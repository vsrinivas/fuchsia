// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_inspect_common::{extract_format_from_env, run_command, StandardOutput},
    ffx_inspect_list_args::ListCommand,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    iquery::commands as iq,
};

#[ffx_plugin(
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn list(
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    cmd: ListCommand,
) -> Result<()> {
    let mut output = StandardOutput::new(extract_format_from_env());
    run_command(rcs_proxy, diagnostics_proxy, iq::ListCommand::from(cmd), &mut output).await
}

#[cfg(test)]
mod test {
    use {
        super::*,
        errors::ResultExt as _,
        ffx_inspect_test_utils::{
            make_lifecycles, setup_fake_diagnostics_bridge, setup_fake_rcs,
            FakeArchiveIteratorResponse, FakeBridgeData, FakeOutput,
        },
        fidl_fuchsia_developer_remotecontrol::{ArchiveIteratorError, BridgeStreamParameters},
        fidl_fuchsia_diagnostics::{ClientSelectorConfiguration, DataType, StreamMode},
        std::sync::Arc,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_empty() {
        let params = BridgeStreamParameters {
            stream_mode: Some(StreamMode::Snapshot),
            data_type: Some(DataType::Lifecycle),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            ..BridgeStreamParameters::EMPTY
        };
        let expected_responses = Arc::new(vec![]);
        let mut output = FakeOutput::new();
        let cmd = ListCommand { manifest: None, with_url: false, accessor_path: None };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![FakeBridgeData::new(
                params,
                expected_responses.clone(),
            )]),
            iq::ListCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        assert_eq!(output.results, vec![String::from("[]")]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_fidl_error() {
        let params = BridgeStreamParameters {
            stream_mode: Some(StreamMode::Snapshot),
            data_type: Some(DataType::Lifecycle),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            ..BridgeStreamParameters::EMPTY
        };
        let expected_responses = Arc::new(vec![FakeArchiveIteratorResponse::new_with_fidl_error()]);
        let mut output = FakeOutput::new();
        let cmd = ListCommand { manifest: None, with_url: false, accessor_path: None };

        assert!(run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![FakeBridgeData::new(
                params,
                expected_responses.clone()
            )]),
            iq::ListCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_iterator_error() {
        let params = BridgeStreamParameters {
            stream_mode: Some(StreamMode::Snapshot),
            data_type: Some(DataType::Lifecycle),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            ..BridgeStreamParameters::EMPTY
        };
        let expected_responses =
            Arc::new(vec![FakeArchiveIteratorResponse::new_with_iterator_error(
                ArchiveIteratorError::GenericError,
            )]);
        let mut output = FakeOutput::new();
        let cmd = ListCommand { manifest: None, with_url: false, accessor_path: None };

        assert!(run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![FakeBridgeData::new(
                params,
                expected_responses.clone()
            )]),
            iq::ListCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_data() {
        let params = BridgeStreamParameters {
            stream_mode: Some(StreamMode::Snapshot),
            data_type: Some(DataType::Lifecycle),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            ..BridgeStreamParameters::EMPTY
        };
        let lifecycles = make_lifecycles();
        let value = serde_json::to_string(&lifecycles).unwrap();
        let expected_responses = Arc::new(vec![FakeArchiveIteratorResponse::new_with_value(value)]);
        let mut output = FakeOutput::new();
        let cmd = ListCommand { manifest: None, with_url: false, accessor_path: None };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![FakeBridgeData::new(
                params,
                expected_responses.clone(),
            )]),
            iq::ListCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            String::from("test/moniker1"),
            String::from("test/moniker3"),
        ])
        .unwrap();
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_data_with_url() {
        let params = BridgeStreamParameters {
            stream_mode: Some(StreamMode::Snapshot),
            data_type: Some(DataType::Lifecycle),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            ..BridgeStreamParameters::EMPTY
        };
        let lifecycles = make_lifecycles();
        let value = serde_json::to_string(&lifecycles).unwrap();
        let expected_responses = Arc::new(vec![FakeArchiveIteratorResponse::new_with_value(value)]);
        let mut output = FakeOutput::new();
        let cmd = ListCommand { manifest: None, with_url: true, accessor_path: None };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![FakeBridgeData::new(
                params,
                expected_responses.clone(),
            )]),
            iq::ListCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            iquery::commands::MonikerWithUrl {
                moniker: String::from("test/moniker1"),
                component_url: String::from("fake-url://test/moniker1"),
            },
            iquery::commands::MonikerWithUrl {
                moniker: String::from("test/moniker3"),
                component_url: String::from("fake-url://test/moniker3"),
            },
        ])
        .unwrap();
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_data_with_manifest_and_archive() {
        let accessor_path = String::from("some/archivist/path");
        let params = BridgeStreamParameters {
            stream_mode: Some(StreamMode::Snapshot),
            data_type: Some(DataType::Lifecycle),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            accessor_path: Some(accessor_path.clone()),
            ..BridgeStreamParameters::EMPTY
        };
        let lifecycles = make_lifecycles();
        let value = serde_json::to_string(&lifecycles).unwrap();
        let expected_responses = Arc::new(vec![FakeArchiveIteratorResponse::new_with_value(value)]);
        let mut output = FakeOutput::new();
        let cmd = ListCommand {
            manifest: Some(String::from("moniker1")),
            with_url: true,
            accessor_path: Some(accessor_path),
        };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![FakeBridgeData::new(
                params,
                expected_responses.clone(),
            )]),
            iq::ListCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![iquery::commands::MonikerWithUrl {
            moniker: String::from("test/moniker1"),
            component_url: String::from("fake-url://test/moniker1"),
        }])
        .unwrap();
        assert_eq!(output.results, vec![expected]);
    }
}
