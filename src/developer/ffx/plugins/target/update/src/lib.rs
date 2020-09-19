// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_update_args as args,
    fidl_fuchsia_update::{
        CheckOptions, Initiator, ManagerProxy, MonitorMarker, MonitorRequest, MonitorRequestStream,
    },
    fidl_fuchsia_update_channelcontrol::ChannelControlProxy,
    fidl_fuchsia_update_ext::State,
    futures::prelude::*,
};

/// Main entry point for the `update` subcommand.
#[ffx_plugin(
    "target_update",
    ManagerProxy = "core/appmgr:out:fuchsia.update.ManagerProxy",
    ChannelControlProxy = "core/appmgr:out:fuchsia.update.channelcontrol.ChannelControl"
)]
pub async fn update_cmd(
    update_manager_proxy: ManagerProxy,
    channel_control_proxy: ChannelControlProxy,
    update_args: args::Update,
) -> Result<(), Error> {
    match update_args.cmd {
        args::Command::Channel(args::Channel { cmd }) => {
            handle_channel_control_cmd(cmd, channel_control_proxy).await?;
        }
        args::Command::CheckNow(check_now) => {
            handle_check_now_cmd(check_now, update_manager_proxy).await?;
        }
        args::Command::ForceInstall(args) => {
            force_install(args.update_pkg_url, args.reboot).await?;
        }
    }
    Ok(())
}

/// Wait for and print state changes. For informational / DX purposes.
async fn monitor_state(mut stream: MonitorRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            MonitorRequest::OnState { state, responder } => {
                responder.send()?;

                let state = State::from(state);

                // Exit if we encounter an error during an update.
                if state.is_error() {
                    anyhow::bail!("Update failed: {:?}", state)
                } else {
                    println!("State: {:?}", state);
                }
            }
        }
    }
    Ok(())
}

/// Handle subcommands for `update channel`.
async fn handle_channel_control_cmd(
    cmd: args::channel::Command,
    channel_control: fidl_fuchsia_update_channelcontrol::ChannelControlProxy,
) -> Result<(), Error> {
    match cmd {
        args::channel::Command::Get(_) => {
            let channel = channel_control.get_current().await?;
            println!("current channel: {}", channel);
        }
        args::channel::Command::Target(_) => {
            let channel = channel_control.get_target().await?;
            println!("target channel: {}", channel);
        }
        args::channel::Command::Set(args::channel::Set { channel }) => {
            channel_control.set_target(&channel).await?;
        }
        args::channel::Command::List(_) => {
            let channels = channel_control.get_target_list().await?;
            if channels.is_empty() {
                println!("known channels list is empty.");
            } else {
                println!("known channels:");
                for channel in channels {
                    println!("{}", channel);
                }
            }
        }
    }
    Ok(())
}

/// If there's a new version available, update to it, printing progress to the
/// console during the process.
async fn handle_check_now_cmd(
    cmd: args::CheckNow,
    update_manager: fidl_fuchsia_update::ManagerProxy,
) -> Result<(), Error> {
    let args::CheckNow { service_initiated, monitor } = cmd;
    let options = CheckOptions {
        initiator: Some(if service_initiated { Initiator::Service } else { Initiator::User }),
        allow_attaching_to_existing_update_check: Some(true),
    };
    let (monitor_client, monitor_server) = if monitor {
        let (client_end, request_stream) =
            fidl::endpoints::create_request_stream::<MonitorMarker>()?;
        (Some(client_end), Some(request_stream))
    } else {
        (None, None)
    };
    if let Err(e) = update_manager.check_now(options, monitor_client).await? {
        anyhow::bail!("Update check failed to start: {:?}", e);
    }
    println!("Checking for an update.");
    if let Some(monitor_server) = monitor_server {
        monitor_state(monitor_server).await?;
    }
    Ok(())
}

/// Change to a specific version, regardless of whether it's newer or older than
/// the current system software.
// TODO(fxb/60019): implement force install.
async fn force_install(_update_pkg_url: String, _reboot: bool) -> Result<(), Error> {
    println!("The force install is not yet implemented in this tool.");
    println!("In the meantime, please use preexisting tools for a force install.");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_update_channelcontrol::ChannelControlRequest;
    use matches::assert_matches;

    async fn perform_channel_control_test<V>(argument: args::channel::Command, verifier: V)
    where
        V: Fn(ChannelControlRequest),
    {
        let (proxy, mut stream) =
            create_proxy_and_stream::<fidl_fuchsia_update_channelcontrol::ChannelControlMarker>()
                .unwrap();
        let fut = async move {
            assert_matches!(handle_channel_control_cmd(argument, proxy).await, Ok(()));
        };
        let stream_fut = async move {
            let result = stream.next().await.unwrap();
            match result {
                Ok(cmd) => verifier(cmd),
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_channel_get() {
        perform_channel_control_test(args::channel::Command::Get(args::channel::Get {}), |cmd| {
            match cmd {
                ChannelControlRequest::GetCurrent { responder } => {
                    responder.send("channel").unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_channel_target() {
        perform_channel_control_test(
            args::channel::Command::Target(args::channel::Target {}),
            |cmd| match cmd {
                ChannelControlRequest::GetTarget { responder } => {
                    responder.send("target-channel").unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_channel_set() {
        perform_channel_control_test(
            args::channel::Command::Set(args::channel::Set { channel: "new-channel".to_string() }),
            |cmd| match cmd {
                ChannelControlRequest::SetTarget { channel, responder } => {
                    assert_eq!(channel, "new-channel");
                    responder.send().unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_channel_list() {
        perform_channel_control_test(args::channel::Command::List(args::channel::List {}), |cmd| {
            match cmd {
                ChannelControlRequest::GetTargetList { responder } => {
                    responder.send(&mut vec!["some-channel", "other-channel"].into_iter()).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .await;
    }
}
