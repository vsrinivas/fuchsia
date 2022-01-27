// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/76549): Replace with GN config once available in an ffx_plugin.
#![deny(unused_results)]

use anyhow::Context as _;
use ffx_core::ffx_plugin;
use ffx_net_test_realm_args as ntr_args;
use fidl_fuchsia_developer_remotecontrol as fremotecontrol;
use fidl_fuchsia_net_test_realm as fntr;

const NET_TEST_REALM_CONTROLLER_SELECTOR_SUFFIX: &str = ":expose:fuchsia.net.test.realm.Controller";

async fn connect_to_protocol<S: fidl::endpoints::ProtocolMarker>(
    remote_control: &fremotecontrol::RemoteControlProxy,
    unparsed_selector: &str,
) -> anyhow::Result<S::Proxy> {
    let (proxy, server_end) = fidl::endpoints::create_proxy::<S>()
        .with_context(|| format!("failed to create proxy to {}", S::NAME))?;
    let selector = selectors::parse_selector::<selectors::VerboseError>(&unparsed_selector)
        .with_context(|| format!("failed to parse selector {}", unparsed_selector))?;
    let _: fremotecontrol::ServiceMatch =
        remote_control.connect(selector, server_end.into_channel()).await?.map_err(|e| {
            anyhow::anyhow!(
                "failed to connect to {} as {}: {:?}. Did you forget to run ffx component start?",
                S::NAME,
                unparsed_selector,
                e
            )
        })?;
    Ok(proxy)
}

#[ffx_plugin("net.test.realm")]
pub async fn net_test_realm(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: ntr_args::Command,
) -> anyhow::Result<()> {
    let ntr_controller_selector =
        format!("{}{}", cmd.component_moniker, NET_TEST_REALM_CONTROLLER_SELECTOR_SUFFIX);
    let controller =
        connect_to_protocol::<fntr::ControllerMarker>(&remote_control, &ntr_controller_selector)
            .await?;
    handle_command(controller, cmd.subcommand).await
}

async fn handle_command(
    controller: fntr::ControllerProxy,
    command: ntr_args::Subcommand,
) -> anyhow::Result<()> {
    let (result_fut, method_name) = match command {
        ntr_args::Subcommand::AddInterface(ntr_args::AddInterface { mac_address, name }) => {
            (controller.add_interface(&mut mac_address.into(), &name), "add_interface")
        }
        ntr_args::Subcommand::JoinMulticastGroup(ntr_args::JoinMulticastGroup {
            address,
            interface_id,
        }) => (
            controller.join_multicast_group(&mut address.into(), interface_id),
            "join_multicast_group",
        ),
        ntr_args::Subcommand::LeaveMulticastGroup(ntr_args::LeaveMulticastGroup {
            address,
            interface_id,
        }) => (
            controller.leave_multicast_group(&mut address.into(), interface_id),
            "leave_multicast_group",
        ),
        ntr_args::Subcommand::Ping(ntr_args::Ping {
            target,
            payload_length,
            timeout,
            interface_name,
        }) => (
            controller.ping(&mut target.into(), payload_length, interface_name.as_deref(), timeout),
            "ping",
        ),
        ntr_args::Subcommand::StartHermeticNetworkRealm(ntr_args::StartHermeticNetworkRealm {
            netstack,
        }) => (controller.start_hermetic_network_realm(netstack), "start_hermetic_network_realm"),
        ntr_args::Subcommand::StartStub(ntr_args::StartStub { component_url }) => {
            (controller.start_stub(&component_url), "start_stub")
        }
        ntr_args::Subcommand::StopHermeticNetworkRealm(ntr_args::StopHermeticNetworkRealm {}) => {
            (controller.stop_hermetic_network_realm(), "stop_hermetic_network_realm")
        }
        ntr_args::Subcommand::StopStub(ntr_args::StopStub {}) => {
            (controller.stop_stub(), "stop_stub")
        }
    };

    result_fut
        .await
        .context(format!("{} failed", method_name))?
        .map_err(|e| anyhow::format_err!("{} error: {:?}", method_name, e))
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_net as fnet;
    use futures::TryStreamExt as _;
    use net_declare::{fidl_ip, fidl_mac};

    const COMPONENT_URL: &'static str =
        "fuchsia-pkg://fuchsia.com/fake-component#meta/fake-component.cm";
    const INTERFACE_ID: u64 = 1;
    const INTERFACE_NAME: &'static str = "eth1";
    const IP_ADDRESS: fnet::IpAddress = fidl_ip!("192.168.0.1");
    const MAC_ADDRESS: fnet::MacAddress = fidl_mac!("02:03:04:05:06:07");

    /// Executes the `command` and routes the emitted request to the provided
    /// `response_handler`.
    ///
    /// The `response_handler` should validate the request and output the
    /// desired response.
    async fn net_test_realm_command_test<F: FnOnce(fntr::ControllerRequest)>(
        command: ntr_args::Subcommand,
        response_handler: F,
    ) {
        let (controller, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<fntr::ControllerMarker>()
                .expect("create_proxy_and_stream failed");
        let op = handle_command(controller, command);
        let op_response = async {
            let request = requests
                .try_next()
                .await
                .expect("controller request stream error")
                .expect("controller request not found");
            response_handler(request);
            Ok(())
        };
        futures::future::try_join(op, op_response).await.expect("net_test_realm command failed");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn add_interface() {
        net_test_realm_command_test(
            ntr_args::Subcommand::AddInterface(ntr_args::AddInterface {
                mac_address: MAC_ADDRESS.into(),
                name: INTERFACE_NAME.to_string(),
            }),
            |request| {
                let (mac_address, name, responder) =
                    request.into_add_interface().expect("expected request of type AddInterface");
                assert_eq!(MAC_ADDRESS, mac_address);
                assert_eq!(INTERFACE_NAME.to_string(), name);
                responder.send(&mut Ok(())).expect("failed to send AddInterface response");
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn join_multicast_group() {
        net_test_realm_command_test(
            ntr_args::Subcommand::JoinMulticastGroup(ntr_args::JoinMulticastGroup {
                address: IP_ADDRESS.into(),
                interface_id: INTERFACE_ID,
            }),
            |request| {
                let (ip_address, interface_id, responder) = request
                    .into_join_multicast_group()
                    .expect("expected request of type JoinMulticastGroup");
                assert_eq!(IP_ADDRESS, ip_address);
                assert_eq!(INTERFACE_ID, interface_id);
                responder.send(&mut Ok(())).expect("failed to send JoinMulticastGroup response");
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn leave_multicast_group() {
        net_test_realm_command_test(
            ntr_args::Subcommand::LeaveMulticastGroup(ntr_args::LeaveMulticastGroup {
                address: IP_ADDRESS.into(),
                interface_id: INTERFACE_ID,
            }),
            |request| {
                let (ip_address, interface_id, responder) = request
                    .into_leave_multicast_group()
                    .expect("expected request of type LeaveMulticastGroup");
                assert_eq!(IP_ADDRESS, ip_address);
                assert_eq!(INTERFACE_ID, interface_id);
                responder.send(&mut Ok(())).expect("failed to send LeaveMulticastGroup response");
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn ping() {
        let expected_payload_length = 100;
        let expected_timeout: i64 = 1000;
        net_test_realm_command_test(
            ntr_args::Subcommand::Ping(ntr_args::Ping {
                target: IP_ADDRESS.into(),
                payload_length: expected_payload_length,
                timeout: expected_timeout,
                interface_name: Some(INTERFACE_NAME.to_string()),
            }),
            |request| {
                let (target, payload_length, interface_name, timeout, responder) =
                    request.into_ping().expect("expected request of type Ping");
                assert_eq!(IP_ADDRESS, target);
                assert_eq!(expected_payload_length, payload_length);
                assert_eq!(Some(INTERFACE_NAME.to_string()), interface_name);
                assert_eq!(expected_timeout, timeout);
                responder.send(&mut Ok(())).expect("failed to send Ping response");
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_hermetic_network_realm() {
        let expected_netstack = fntr::Netstack::V2;
        net_test_realm_command_test(
            ntr_args::Subcommand::StartHermeticNetworkRealm(ntr_args::StartHermeticNetworkRealm {
                netstack: expected_netstack,
            }),
            |request| {
                let (netstack, responder) = request
                    .into_start_hermetic_network_realm()
                    .expect("expected request of type StartHermeticNetworkRealm");
                assert_eq!(expected_netstack, netstack);
                responder
                    .send(&mut Ok(()))
                    .expect("failed to send StartHermeticNetworkRealm response");
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_stub() {
        net_test_realm_command_test(
            ntr_args::Subcommand::StartStub(ntr_args::StartStub {
                component_url: COMPONENT_URL.to_string(),
            }),
            |request| {
                let (component_url, responder) =
                    request.into_start_stub().expect("expected request of type StartStub");
                assert_eq!(COMPONENT_URL.to_string(), component_url);
                responder.send(&mut Ok(())).expect("failed to send StartStub response");
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn stop_hermetic_network_realm() {
        net_test_realm_command_test(
            ntr_args::Subcommand::StopHermeticNetworkRealm(ntr_args::StopHermeticNetworkRealm {}),
            |request| {
                request
                    .into_stop_hermetic_network_realm()
                    .expect("expected request of type StopHermeticNetworkRealm")
                    .send(&mut Ok(()))
                    .expect("failed to send StopHermeticNetworkRealm response");
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn stop_stub() {
        net_test_realm_command_test(
            ntr_args::Subcommand::StopStub(ntr_args::StopStub {}),
            |request| {
                request
                    .into_stop_stub()
                    .expect("expected request of type StopStub")
                    .send(&mut Ok(()))
                    .expect("failed to send StopStub response");
            },
        )
        .await;
    }
}
