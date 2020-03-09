// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_update::{
    Initiator, ManagerMarker, ManagerProxy, MonitorEvent, MonitorMarker, MonitorProxy, Options,
    State,
};
use fidl_fuchsia_update_channelcontrol::{ChannelControlMarker, ChannelControlProxy};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use futures::prelude::*;

mod args;

fn print_state(state: State) {
    if let Some(state) = state.state {
        println!("State: {:?}", state);
    }
    if let Some(version) = state.version_available {
        println!("Version available: {}", version);
    }
}

async fn monitor_state(monitor: MonitorProxy) -> Result<(), Error> {
    let mut stream = monitor.take_event_stream();
    while let Some(event) = stream.try_next().await? {
        match event {
            MonitorEvent::OnState { state } => {
                print_state(state);
            }
        }
    }
    Ok(())
}

async fn handle_channel_control_cmd(
    cmd: args::channel::Command,
    channel_control: ChannelControlProxy,
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

async fn handle_manager_cmd(cmd: args::Command, update_manager: ManagerProxy) -> Result<(), Error> {
    match cmd {
        args::Command::State(_) => {
            let state = update_manager.get_state().await?;
            print_state(state);
        }
        args::Command::CheckNow(args::CheckNow { service_initiated, monitor }) => {
            let options = Options {
                initiator: Some(if service_initiated {
                    Initiator::Service
                } else {
                    Initiator::User
                }),
            };
            if monitor {
                let (client_proxy, server_end) = fidl::endpoints::create_proxy::<MonitorMarker>()?;
                let result = update_manager.check_now(options, Some(server_end)).await?;
                println!("Check started result: {:?}", result);
                monitor_state(client_proxy).await?;
            } else {
                let result = update_manager.check_now(options, None).await?;
                println!("Check started result: {:?}", result);
            }
        }
        args::Command::Monitor(_) => {
            let (client_proxy, server_end) = fidl::endpoints::create_proxy::<MonitorMarker>()?;
            update_manager.add_monitor(server_end)?;
            monitor_state(client_proxy).await?;
        }
        _ => {}
    }
    Ok(())
}

async fn handle_cmd(cmd: args::Command) -> Result<(), Error> {
    match cmd {
        args::Command::Channel(args::Channel { cmd }) => {
            let channel_control = connect_to_service::<ChannelControlMarker>()
                .context("Failed to connect to channel control service")?;

            handle_channel_control_cmd(cmd, channel_control).await?;
        }
        args::Command::State(_) | args::Command::CheckNow(_) | args::Command::Monitor(_) => {
            let update_manager = connect_to_service::<ManagerMarker>()
                .context("Failed to connect to omaha client manager service")?;
            handle_manager_cmd(cmd, update_manager).await?;
        }
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args::Update { cmd } = argh::from_env();
    handle_cmd(cmd).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_update::{CheckStartedResult, ManagerRequest, ManagerState};
    use fidl_fuchsia_update_channelcontrol::ChannelControlRequest;
    use matches::assert_matches;

    async fn perform_channel_control_test<V>(argument: args::channel::Command, verifier: V)
    where
        V: Fn(ChannelControlRequest),
    {
        let (proxy, mut stream) = create_proxy_and_stream::<ChannelControlMarker>().unwrap();
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

    #[fasync::run_singlethreaded(test)]
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

    #[fasync::run_singlethreaded(test)]
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

    #[fasync::run_singlethreaded(test)]
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

    #[fasync::run_singlethreaded(test)]
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

    async fn perform_manager_test<V>(argument: args::Command, verifier: V)
    where
        V: Fn(ManagerRequest),
    {
        let (proxy, mut stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        let fut = async move {
            assert_matches!(handle_manager_cmd(argument, proxy).await, Ok(()));
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

    #[fasync::run_singlethreaded(test)]
    async fn test_state() {
        perform_manager_test(args::Command::State(args::State {}), |cmd| match cmd {
            ManagerRequest::GetState { responder } => {
                responder
                    .send(State { state: Some(ManagerState::Idle), version_available: None })
                    .unwrap();
            }
            request => panic!("Unexpected request: {:?}", request),
        })
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_state_error() {
        let (proxy, mut stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        let fut = async move {
            let cmd = args::Command::State(args::State {});
            assert_matches!(handle_manager_cmd(cmd, proxy).await, Err(_));
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(ManagerRequest::GetState { .. }) => {
                    // Don't send response.
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now() {
        perform_manager_test(
            args::Command::CheckNow(args::CheckNow { service_initiated: false, monitor: false }),
            |cmd| match cmd {
                ManagerRequest::CheckNow { options, monitor, responder } => {
                    assert_eq!(options.initiator, Some(Initiator::User));
                    assert_eq!(monitor, None);
                    responder.send(CheckStartedResult::Started).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            },
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_monitor() {
        perform_manager_test(
            args::Command::CheckNow(args::CheckNow { service_initiated: false, monitor: true }),
            |cmd| match cmd {
                ManagerRequest::CheckNow { options, monitor, responder } => {
                    assert_eq!(options.initiator, Some(Initiator::User));
                    assert!(monitor.is_some());
                    responder.send(CheckStartedResult::Started).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            },
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_service_initiated() {
        perform_manager_test(
            args::Command::CheckNow(args::CheckNow { service_initiated: true, monitor: false }),
            |cmd| match cmd {
                ManagerRequest::CheckNow { options, monitor, responder } => {
                    assert_eq!(options.initiator, Some(Initiator::Service));
                    assert_eq!(monitor, None);
                    responder.send(CheckStartedResult::Started).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            },
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_monitor() {
        perform_manager_test(args::Command::Monitor(args::Monitor {}), |cmd| match cmd {
            ManagerRequest::AddMonitor { monitor, .. } => {
                let (_stream, handle) = monitor.into_stream_and_control_handle().unwrap();
                handle
                    .send_on_state(State {
                        state: Some(ManagerState::CheckingForUpdates),
                        version_available: None,
                    })
                    .unwrap();
            }
            request => panic!("Unexpected request: {:?}", request),
        })
        .await;
    }
}
