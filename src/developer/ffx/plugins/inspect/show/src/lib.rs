// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_inspect_common::{extract_format_from_env, run_command, StandardOutput},
    ffx_inspect_show_args::ShowCommand,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    iquery::commands as iq,
};

#[ffx_plugin(
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn show(
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    cmd: ShowCommand,
) -> Result<()> {
    let mut output = StandardOutput::new(extract_format_from_env());
    run_command(rcs_proxy, diagnostics_proxy, iq::ShowCommand::from(cmd), &mut output).await
}

#[cfg(test)]
mod test {
    use {
        super::*,
        errors::ResultExt as _,
        ffx_inspect_test_utils::{
            inspect_bridge_data, lifecycle_bridge_data, make_inspect_with_length, make_inspects,
            setup_fake_diagnostics_bridge, setup_fake_rcs, FakeOutput,
        },
        fidl_fuchsia_diagnostics::{ClientSelectorConfiguration, SelectorArgument},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show_no_parameters() {
        let mut output = FakeOutput::new();
        let cmd = ShowCommand { manifest: None, selectors: vec![], accessor_path: None };
        let lifecycle_data = lifecycle_bridge_data();
        let mut inspects = make_inspects();
        let inspect_data =
            inspect_bridge_data(ClientSelectorConfiguration::SelectAll(true), inspects.clone());
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![lifecycle_data, inspect_data]),
            iq::ShowCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        inspects.sort_by(|a, b| a.moniker.cmp(&b.moniker));
        let expected = serde_json::to_string(&inspects).unwrap();
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show_unknown_manifest() {
        let mut output = FakeOutput::new();
        let cmd = ShowCommand {
            manifest: Some(String::from("some-bad-moniker")),
            selectors: vec![],
            accessor_path: None,
        };
        let lifecycle_data = lifecycle_bridge_data();
        let inspects = make_inspects();
        let inspect_data =
            inspect_bridge_data(ClientSelectorConfiguration::SelectAll(true), inspects.clone());
        assert!(run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![lifecycle_data, inspect_data]),
            iq::ShowCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show_with_manifest_that_exists() {
        let mut output = FakeOutput::new();
        let cmd = ShowCommand {
            manifest: Some(String::from("moniker1")),
            selectors: vec![],
            accessor_path: None,
        };
        let lifecycle_data = lifecycle_bridge_data();
        let mut inspects = vec![
            make_inspect_with_length(String::from("test/moniker1"), 1, 20),
            make_inspect_with_length(String::from("test/moniker1"), 3, 10),
            make_inspect_with_length(String::from("test/moniker1"), 6, 30),
        ];
        let inspect_data = inspect_bridge_data(
            ClientSelectorConfiguration::Selectors(vec![SelectorArgument::RawSelector(
                String::from("test/moniker1:root"),
            )]),
            inspects.clone(),
        );
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![lifecycle_data, inspect_data]),
            iq::ShowCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        inspects.sort_by(|a, b| a.moniker.cmp(&b.moniker));
        let expected = serde_json::to_string(&inspects).unwrap();
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show_with_selectors_with_no_data() {
        let mut output = FakeOutput::new();
        let cmd = ShowCommand {
            manifest: None,
            selectors: vec![String::from("test/moniker1:name:hello_not_real")],
            accessor_path: None,
        };
        let lifecycle_data = lifecycle_bridge_data();
        let inspect_data = inspect_bridge_data(
            ClientSelectorConfiguration::Selectors(vec![SelectorArgument::RawSelector(
                String::from("test/moniker1:name:hello_not_real"),
            )]),
            vec![],
        );
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![lifecycle_data, inspect_data]),
            iq::ShowCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = String::from("[]");
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show_with_selectors_with_data() {
        let mut output = FakeOutput::new();
        let cmd = ShowCommand {
            manifest: None,
            selectors: vec![String::from("test/moniker1:name:hello_6")],
            accessor_path: None,
        };
        let lifecycle_data = lifecycle_bridge_data();
        let mut inspects = vec![make_inspect_with_length(String::from("test/moniker1"), 6, 30)];
        let inspect_data = inspect_bridge_data(
            ClientSelectorConfiguration::Selectors(vec![SelectorArgument::RawSelector(
                String::from("test/moniker1:name:hello_6"),
            )]),
            inspects.clone(),
        );
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![lifecycle_data, inspect_data]),
            iq::ShowCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        inspects.sort_by(|a, b| a.moniker.cmp(&b.moniker));
        let expected = serde_json::to_string(&inspects).unwrap();
        assert_eq!(output.results, vec![expected]);
    }
}
