// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
            NetworkConfig, NetworkContextMarker, NetworkContextProxy, NetworkManagerMarker,
            NetworkProxy,
        },
        fidl_fuchsia_netemul_sandbox::{SandboxMarker, SandboxProxy},
        fidl_fuchsia_netemul_sync::{BusMarker, BusProxy, SyncManagerMarker, SyncManagerProxy},
        fidl_fuchsia_netstack::NetstackMarker,
        fuchsia_async as fasync,
        fuchsia_component::client,
        fuchsia_zircon as zx,
    };

    fn create_named_env_with_netstack(
        sandbox: &SandboxProxy,
        name: Option<String>,
    ) -> Result<ManagedEnvironmentProxy, Error> {
        let (env, env_server_end) = fidl::endpoints::create_proxy::<ManagedEnvironmentMarker>()?;
        let services = vec![LaunchService {
            name: String::from("fuchsia.netstack.Netstack"),
            url: String::from("fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx"),
            arguments: None,
        }];
        sandbox.create_environment(
            env_server_end,
            EnvironmentOptions {
                name: name, // don't care about the name, let it be created by itself
                services: Some(services),
                devices: None,
                inherit_parent_launch_services: Some(false),
                logger_options: Some(LoggerOptions {
                    enabled: Some(true),
                    klogs_enabled: Some(false),
                    filter_options: None,
                    syslog_output: Some(true),
                }),
            },
        )?;

        Ok(env)
    }

    fn create_env_with_netstack(sandbox: &SandboxProxy) -> Result<ManagedEnvironmentProxy, Error> {
        create_named_env_with_netstack(sandbox, None)
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
        let (status, network) = netmgr.create_network(name, config).await?;
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
        get_network_from_context(&netctx, name).await
    }

    async fn get_network_from_context<'a>(
        netctx: &'a NetworkContextProxy,
        name: &'a str,
    ) -> Result<NetworkProxy, Error> {
        let (netmgr, netmgr_server_end) = fidl::endpoints::create_proxy::<NetworkManagerMarker>()?;
        netctx.get_network_manager(netmgr_server_end)?;
        let network = netmgr.get_network(name).await?;

        Ok(network.ok_or_else(|| format_err!("can't create network"))?.into_proxy()?)
    }

    async fn get_on_bus_from_env<'a>(
        env: &'a ManagedEnvironmentProxy,
        bus_name: &'static str,
        name: &'static str,
    ) -> Result<BusProxy, Error> {
        let (syncmgr, syncmgr_server_end) = fidl::endpoints::create_proxy::<SyncManagerMarker>()?;
        env.connect_to_service(SyncManagerMarker::NAME, syncmgr_server_end.into_channel())?;
        let (bus, bus_server_end) = fidl::endpoints::create_proxy::<BusMarker>()?;
        let () = syncmgr.bus_subscribe(bus_name, name, bus_server_end)?;
        // do something to ensure ordering
        let _ = bus.get_clients().await?;
        Ok(bus)
    }

    async fn get_on_bus_and_list_clients<'a>(
        syncmgr: &'a SyncManagerProxy,
        bus_name: &'static str,
        name: &'static str,
    ) -> Result<Vec<String>, Error> {
        let (bus, bus_server_end) = fidl::endpoints::create_proxy::<BusMarker>()?;
        let () = syncmgr.bus_subscribe(bus_name, name, bus_server_end)?;
        Ok(bus.get_clients().await?)
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
            netstack.get_interfaces().await.context("can't list netstack interfaces").unwrap();
        assert!(
            ifs.len() <= 1,
            "brand new netstack should not have any interfaces except for loopback"
        );
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn environment_netctx_sandboxing() {
        let sandbox =
            client::connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox 1");

        let sandbox2 =
            client::connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox 2");

        let (netctx1, netctx_server_end) = fidl::endpoints::create_proxy::<NetworkContextMarker>()
            .expect("can't create context proxy 1");
        sandbox.get_network_context(netctx_server_end).expect("failed to get network context 1");

        let (netctx2, netctx_server_end) = fidl::endpoints::create_proxy::<NetworkContextMarker>()
            .expect("can't create context proxy 2");
        sandbox2.get_network_context(netctx_server_end).expect("failed to get network context 2");

        let env1 = create_env_with_netstack(&sandbox).expect("can't create env 1");
        let env2 = create_env_with_netstack(&sandbox).expect("can't create env 2");
        let env3 = create_env_with_netstack(&sandbox2).expect("can't create env 3");

        let _net = create_network(&env1, "network").await.expect("failed to create network");
        let net1_retrieve = get_network(&env1, "network").await;
        assert!(net1_retrieve.is_ok(), "can retrieve net from env1");
        let net2_retrieve = get_network(&env2, "network").await;
        assert!(net2_retrieve.is_ok(), "can retrieve net from env2");
        let net3_retrieve = get_network(&env3, "network").await;
        assert!(net3_retrieve.is_err(), "net should not exist in env3");

        get_network_from_context(&netctx1, "network")
            .await
            .expect("Should be able to retrieve net from sandbox 1");
        get_network_from_context(&netctx2, "network")
            .await
            .expect_err("Shouldn't be able retrieve net from sandbox 2");
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn environment_syncmgr_sandboxing() {
        const BUS_NAME: &'static str = "bus";

        let sandbox =
            client::connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox 1");

        let sandbox2 =
            client::connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox 2");

        let (sync1, sync_server_end) = fidl::endpoints::create_proxy::<SyncManagerMarker>()
            .expect("can't create sync manager proxy 1");
        sandbox.get_sync_manager(sync_server_end).expect("failed to get sync manager 1");

        let (sync2, sync_server_end) = fidl::endpoints::create_proxy::<SyncManagerMarker>()
            .expect("can't create sync manager proxy 2");
        sandbox2.get_sync_manager(sync_server_end).expect("failed to get sync manager 2");

        let env1 = create_env_with_netstack(&sandbox).expect("can't create env 1");
        let env2 = create_env_with_netstack(&sandbox).expect("can't create env 2");
        let env3 = create_env_with_netstack(&sandbox2).expect("can't create env 3");

        let _b_e1 = get_on_bus_from_env(&env1, BUS_NAME, "e1").await.expect("can get on bus as e1");
        let _b_e2 = get_on_bus_from_env(&env2, BUS_NAME, "e2").await.expect("can get on bus as e2");
        let _b_e3 = get_on_bus_from_env(&env3, BUS_NAME, "e3").await.expect("can get on bus as e3");

        let clients_1 =
            get_on_bus_and_list_clients(&sync1, BUS_NAME, "s1").await.expect("can get clients 1");
        let clients_2 =
            get_on_bus_and_list_clients(&sync2, BUS_NAME, "s2").await.expect("can get clients 2");

        assert_eq!(3, clients_1.len());
        assert_eq!(2, clients_2.len());
        assert!(clients_1.iter().any(|c| c == "e1"));
        assert!(clients_1.iter().any(|c| c == "e2"));
        assert!(clients_2.iter().any(|c| c == "e3"));
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn same_environment_name_fails() {
        let sandbox =
            client::connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox");
        let env_name = Some("env_a".to_string());

        // environment creation should work for both, but doing anything on
        // env2 should fail:
        let env1 =
            create_named_env_with_netstack(&sandbox, env_name.clone()).expect("can't create env 1");
        let env2 = create_named_env_with_netstack(&sandbox, env_name).expect("can't create env 2");

        let _net1 =
            create_network(&env1, "network").await.expect("failed to create network on env 1");
        let _net2 = create_network(&env2, "network2")
            .await
            .expect_err("should've failed to create network on env 2");
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn same_environment_name_succeeds_in_different_sandboxes() {
        let sandbox =
            client::connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox");
        let sandbox2 =
            client::connect_to_service::<SandboxMarker>().expect("Can't connect to sandbox 2");
        let env_name = Some("env_a".to_string());

        let env1 =
            create_named_env_with_netstack(&sandbox, env_name.clone()).expect("can't create env 1");
        let env2 = create_named_env_with_netstack(&sandbox2, env_name).expect("can't create env 2");

        let _net1 =
            create_network(&env1, "network").await.expect("failed to create network on env 1");
        let _net2 =
            create_network(&env2, "network2").await.expect("failed to create network on env 2");
    }
}
