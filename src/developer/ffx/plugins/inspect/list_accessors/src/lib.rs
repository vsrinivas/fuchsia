// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_inspect_common::{extract_format_from_env, run_command, StandardOutput},
    ffx_inspect_list_accessors_args::ListAccessorsCommand,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    iquery::commands as iq,
};

#[ffx_plugin(
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn list_accessors(
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    cmd: ListAccessorsCommand,
) -> Result<()> {
    let mut output = StandardOutput::new(extract_format_from_env());
    run_command(rcs_proxy, diagnostics_proxy, iq::ListAccessorsCommand::from(cmd), &mut output)
        .await
}

#[cfg(test)]
mod test {
    use {
        super::*,
        ffx_inspect_test_utils::{setup_fake_diagnostics_bridge, setup_fake_rcs, FakeOutput},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_accessors_no_parameters() {
        let mut output = FakeOutput::new();
        let cmd = ListAccessorsCommand { paths: vec![] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            String::from("/hub-v2/dir1/fuchsia.diagnostics.FooBarArchiveAccessor"),
            String::from("/hub-v2/dir2/child3/fuchsia.diagnostics.Some.Other.ArchiveAccessor"),
            String::from("/hub-v2/dir2/child5/fuchsia.diagnostics.OneMoreArchiveAccessor"),
        ])
        .unwrap();
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_accessors_subdirectory() {
        let mut output = FakeOutput::new();
        let cmd = ListAccessorsCommand { paths: vec!["dir2".into()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            String::from("/hub-v2/dir2/child3/fuchsia.diagnostics.Some.Other.ArchiveAccessor"),
            String::from("/hub-v2/dir2/child5/fuchsia.diagnostics.OneMoreArchiveAccessor"),
        ])
        .unwrap();
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_accessors_deeper_subdirectory() {
        let mut output = FakeOutput::new();
        let cmd = ListAccessorsCommand { paths: vec!["dir2/child3".into()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![String::from(
            "/hub-v2/dir2/child3/fuchsia.diagnostics.Some.Other.ArchiveAccessor",
        )])
        .unwrap();
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_accessors_multiple_paths() {
        let mut output = FakeOutput::new();
        let cmd = ListAccessorsCommand { paths: vec!["dir1".into(), "dir2/child5".into()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = serde_json::to_string(&vec![
            String::from("/hub-v2/dir1/fuchsia.diagnostics.FooBarArchiveAccessor"),
            String::from("/hub-v2/dir2/child5/fuchsia.diagnostics.OneMoreArchiveAccessor"),
        ])
        .unwrap();
        assert_eq!(output.results, vec![expected]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_accessors_path_with_no_accessors() {
        let mut output = FakeOutput::new();
        let cmd = ListAccessorsCommand { paths: vec!["this/is/bad".into()] };
        run_command(
            setup_fake_rcs(),
            setup_fake_diagnostics_bridge(vec![]),
            iq::ListAccessorsCommand::from(cmd),
            &mut output,
        )
        .await
        .unwrap();

        let expected = "[]".to_owned();
        assert_eq!(output.results, vec![expected]);
    }
}
