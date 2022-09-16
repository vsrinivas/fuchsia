// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, errors::ffx_error, ffx_core::ffx_plugin,
    fidl_fuchsia_power_manager_debug as fdebug,
};

#[ffx_plugin(
    fdebug::DebugProxy = "bootstrap/power_manager:expose:fuchsia.power.manager.debug.Debug"
)]
pub async fn debugcmd(
    proxy: fdebug::DebugProxy,
    cmd: ffx_power_manager_args::PowerManagerDebugCommand,
) -> Result<()> {
    proxy
        .message(&cmd.node_name, &cmd.command, &mut cmd.args.iter().map(|s| s.as_ref()))
        .await?
        .map_err(|e| match e {
            fdebug::MessageError::Generic => ffx_error!("Generic error occurred"),
            fdebug::MessageError::InvalidNodeName => {
                ffx_error!("Invalid node name '{}'", cmd.node_name)
            }
            fdebug::MessageError::UnsupportedCommand => {
                ffx_error!("Unsupported command '{}' for node '{}'", cmd.command, cmd.node_name)
            }
            fdebug::MessageError::InvalidCommandArgs => {
                ffx_error!("Invalid arguments for command '{}'", cmd.command)
            }
            e => ffx_error!("Unknown error: {:?}", e),
        })?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_debugcmd() {
        let command_request = ffx_power_manager_args::PowerManagerDebugCommand {
            node_name: "test_node_name".to_string(),
            command: "test_command".to_string(),
            args: vec!["test_arg_1".to_string(), "test_arg_2".to_string()],
        };

        let debug_proxy = setup_fake_proxy(move |req| match req {
            fdebug::DebugRequest::Message { node_name, command, args, responder, .. } => {
                assert_eq!(node_name, "test_node_name");
                assert_eq!(command, "test_command");
                assert_eq!(args, vec!["test_arg_1", "test_arg_2"]);
                let _ = responder.send(&mut Ok(()));
            }
        });

        debugcmd(debug_proxy, command_request).await.unwrap();
    }
}
