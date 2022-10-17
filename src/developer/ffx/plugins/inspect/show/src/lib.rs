// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_inspect_common::run_command,
    ffx_inspect_show_args::ShowCommand,
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    iquery::commands as iq,
};

#[ffx_plugin(
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn show(
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    #[ffx(machine = Vec<ShowCommandResultItem>)] writer: Writer,
    cmd: ShowCommand,
) -> Result<()> {
    run_command(rcs_proxy, diagnostics_proxy, iq::ShowCommand::from(cmd), writer).await
}

#[cfg(test)]
mod test {
    use {
        super::*,
        errors::ResultExt as _,
        ffx_inspect_test_utils::{
            inspect_bridge_data, make_inspect_with_length, make_inspects,
            make_inspects_for_lifecycle, setup_fake_diagnostics_bridge, setup_fake_rcs,
        },
        ffx_writer::Format,
        fidl_fuchsia_diagnostics::{ClientSelectorConfiguration, SelectorArgument},
    };

    #[fuchsia::test]
    async fn test_show_no_parameters() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ShowCommand { manifest: None, selectors: vec![], file: None, accessor: None };
        let mut inspects = make_inspects();
        let inspect_data =
            inspect_bridge_data(ClientSelectorConfiguration::SelectAll(true), inspects.clone());
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![inspect_data]),
            iq::ShowCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        inspects.sort_by(|a, b| a.moniker.cmp(&b.moniker));
        let expected = serde_json::to_string(&inspects).unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_show_with_valid_file_name() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ShowCommand {
            manifest: None,
            selectors: vec![],
            file: Some(String::from("fuchsia.inspect.Tree")),
            accessor: None,
        };
        let mut inspects = make_inspects();
        let mut inspect_with_file_name =
            make_inspect_with_length(String::from("test/moniker1"), 1, 20);
        inspect_with_file_name.metadata.filename = String::from("fuchsia.inspect.Tree");
        inspects.push(inspect_with_file_name.clone());
        let inspect_data =
            inspect_bridge_data(ClientSelectorConfiguration::SelectAll(true), inspects.clone());
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![inspect_data]),
            iq::ShowCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        inspects.sort_by(|a, b| a.moniker.cmp(&b.moniker));
        let expected = serde_json::to_string(&vec![&inspect_with_file_name]).unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_show_with_invalid_file_name() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ShowCommand {
            manifest: None,
            selectors: vec![],
            file: Some(String::from("some_thing")),
            accessor: None,
        };
        let mut inspects = make_inspects();
        let mut inspect_with_file_name =
            make_inspect_with_length(String::from("test/moniker1"), 1, 20);
        inspect_with_file_name.metadata.filename = String::from("fuchsia.inspect.Tree");
        inspects.push(inspect_with_file_name);
        let inspect_data =
            inspect_bridge_data(ClientSelectorConfiguration::SelectAll(true), inspects.clone());
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![inspect_data]),
            iq::ShowCommand::from(cmd),
            writer.clone(),
        )
        .await
        .unwrap();

        inspects.sort_by(|a, b| a.moniker.cmp(&b.moniker));
        let expected = String::from("[]");
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_show_unknown_manifest() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ShowCommand {
            manifest: Some(String::from("some-bad-moniker")),
            selectors: vec![],
            file: None,
            accessor: None,
        };
        let lifecycle_data = inspect_bridge_data(
            ClientSelectorConfiguration::SelectAll(true),
            make_inspects_for_lifecycle(),
        );
        let inspects = make_inspects();
        let inspect_data =
            inspect_bridge_data(ClientSelectorConfiguration::SelectAll(true), inspects.clone());
        assert!(run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![lifecycle_data, inspect_data]),
            iq::ShowCommand::from(cmd),
            writer
        )
        .await
        .unwrap_err()
        .ffx_error()
        .is_some());
    }

    #[fuchsia::test]
    async fn test_show_with_manifest_that_exists() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ShowCommand {
            manifest: Some(String::from("moniker1")),
            selectors: vec![],
            file: None,
            accessor: None,
        };
        let lifecycle_data = inspect_bridge_data(
            ClientSelectorConfiguration::SelectAll(true),
            make_inspects_for_lifecycle(),
        );
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
            writer.clone(),
        )
        .await
        .unwrap();

        inspects.sort_by(|a, b| a.moniker.cmp(&b.moniker));
        let expected = serde_json::to_string(&inspects).unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_show_with_selectors_with_no_data() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ShowCommand {
            manifest: None,
            selectors: vec![String::from("test/moniker1:name:hello_not_real")],
            file: None,
            accessor: None,
        };
        let lifecycle_data = inspect_bridge_data(
            ClientSelectorConfiguration::SelectAll(true),
            make_inspects_for_lifecycle(),
        );
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
            writer.clone(),
        )
        .await
        .unwrap();

        let expected = String::from("[]");
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }

    #[fuchsia::test]
    async fn test_show_with_selectors_with_data() {
        let writer = Writer::new_test(Some(Format::Json));
        let cmd = ShowCommand {
            manifest: None,
            selectors: vec![String::from("test/moniker1:name:hello_6")],
            file: None,
            accessor: None,
        };
        let lifecycle_data = inspect_bridge_data(
            ClientSelectorConfiguration::SelectAll(true),
            make_inspects_for_lifecycle(),
        );
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
            writer.clone(),
        )
        .await
        .unwrap();

        inspects.sort_by(|a, b| a.moniker.cmp(&b.moniker));
        let expected = serde_json::to_string(&inspects).unwrap();
        let output = writer.test_output().expect("unable to get test output.");
        assert_eq!(output, expected);
    }
}
