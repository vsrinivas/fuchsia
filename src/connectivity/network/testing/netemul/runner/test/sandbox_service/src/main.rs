// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

fn main() {
    // We're only using this binary to show rust unit tests using the sandbox_service.
    panic!("This program does nothing");
}

#[cfg(test)]
mod tests {
    use {
        failure::{format_err, Error, ResultExt},
        fidl::endpoints::ServiceMarker,
        fidl_fuchsia_netemul_environment::{
            EnvironmentOptions, LaunchService, LoggerOptions, ManagedEnvironmentMarker,
            ManagedEnvironmentProxy,
        },
        fidl_fuchsia_netemul_network::{
            NetworkConfig, NetworkContextMarker, NetworkManagerMarker, NetworkProxy,
        },
        fidl_fuchsia_netemul_sandbox::{SandboxMarker, SandboxProxy},
        fidl_fuchsia_netstack::NetstackMarker,
        fuchsia_async as fasync,
        fuchsia_component::client,
        fuchsia_zircon as zx,
    };

    fn create_env_with_netstack(sandbox: &SandboxProxy) -> Result<ManagedEnvironmentProxy, Error> {
        let (env, env_server_end) = fidl::endpoints::create_proxy::<ManagedEnvironmentMarker>()?;
        let services = vec![LaunchService {
            name: String::from("fuchsia.netstack.Netstack"),
            url: String::from("fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx"),
            arguments: None,
        }];
        sandbox.create_environment(
            env_server_end,
            EnvironmentOptions {
                name: None, // don't care about the name, let it be created by itself
                services: Some(services),
                devices: None,
                inherit_parent_launch_services: Some(false),
                logger_options: Some(LoggerOptions {
                    enabled: Some(true),
                    klogs_enabled: Some(false),
                    filter_options: None,
                    syslog_output: None,
                }),
            },
        )?;

        Ok(env)
    }

    async fn create_network<'a>(
        env: &'a ManagedEnvironmentProxy,
        name: &'a str,
    ) -> Result<NetworkProxy, Error> {
        let (netctx, netctx_server_end) = fidl::endpoints::create_proxy::<NetworkContextMarker>()?;
        env.connect_to_service(NetworkContextMarker::NAME, netctx_server_end.into_channel())?;
        let (netmgr, netmgr_server_end) = fidl::endpoints::create_proxy::<NetworkManagerMarker>()?;
        netctx.get_network_manager(netmgr_server_end)?;
        let config = NetworkConfig { latency: None, packet_loss: None, reorder: None };
        let (status, network) = await!(netmgr.create_network(name, config))?;
        match status {
            zx::sys::ZX_OK => Ok(network.unwrap().into_proxy()?),
            _ => Err(format_err!("Create network failed")),
        }
    }

    async fn get_network<'a>(
        env: &'a ManagedEnvironmentProxy,
        name: &'a str,
    ) -> Result<NetworkProxy, Error> {
        let (netctx, netctx_server_end) = fidl::endpoints::create_proxy::<NetworkContextMarker>()?;
        env.connect_to_service(NetworkContextMarker::NAME, netctx_server_end.into_channel())?;
        let (netmgr, netmgr_server_end) = fidl::endpoints::create_proxy::<NetworkManagerMarker>()?;
        netctx.get_network_manager(netmgr_server_end)?;
        let network = await!(netmgr.get_network(name))?;

        Ok(network.ok_or_else(|| format_err!("can't create network"))?.into_proxy()?)
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn can_use_environment_services() {
        let sandbox = client::connect_to_service::<SandboxMarker>()
            .context("Can't connect to sandbox")
            .unwrap();
        let env = create_env_with_netstack(&sandbox).unwrap();
        let (netstack, netstack_server_end) =
            fidl::endpoints::create_proxy::<NetstackMarker>().unwrap();
        env.connect_to_service(NetstackMarker::NAME, netstack_server_end.into_channel())
            .context("Can't connect to netstack")
            .unwrap();
        let ifs =
            await!(netstack.get_interfaces()).context("can't list netstack interfaces").unwrap();
        assert!(
            ifs.len() <= 1,
            "brand new netstack should not have any interfaces except for loopback"
        );
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn environment_sandboxing() {
        let sandbox = client::connect_to_service::<SandboxMarker>()
            .context("Can't connect to sandbox")
            .unwrap();
        let sandbox2 = client::connect_to_service::<SandboxMarker>()
            .context("Can't connect to sandbox 2")
            .unwrap();
        let env1 = create_env_with_netstack(&sandbox).unwrap();
        let env2 = create_env_with_netstack(&sandbox).unwrap();
        let env3 = create_env_with_netstack(&sandbox2).unwrap();

        let _net1 = await!(create_network(&env1, "network")).unwrap();
        let net1_retrieve = await!(get_network(&env1, "network"));
        assert!(net1_retrieve.is_ok(), "can retrieve net1 from env1");
        let net2_retrieve = await!(get_network(&env2, "network"));
        assert!(net2_retrieve.is_ok(), "can retrieve net1 from env2");
        let net3_retrieve = await!(get_network(&env3, "network"));
        assert!(net3_retrieve.is_err(), "net1 should not exist in env3");
    }
}
