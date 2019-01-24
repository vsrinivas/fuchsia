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
            EnvironmentOptions, LaunchService, ManagedEnvironmentMarker, ManagedEnvironmentProxy,
        },
        fidl_fuchsia_netemul_network::{
            NetworkConfig, NetworkContextMarker, NetworkManagerMarker, NetworkProxy,
        },
        fidl_fuchsia_netemul_sandbox::{LaunchOptions, SandboxMarker, SandboxProxy},
        fidl_fuchsia_netemul_sync::{BusMarker, Event, SyncManagerMarker},
        fidl_fuchsia_netstack::NetstackMarker,
        fidl_fuchsia_sys::TerminationReason,
        fuchsia_app::client,
        fuchsia_async::{self as fasync, TimeoutExt},
        fuchsia_zircon::{self as zx, DurationNum},
    };

    const DUMMY_PROC: &'static str =
        "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/dummy_proc.cmx";
    const TEST_TIMEOUT: i64 = 2;
    const TEST_BUS: &'static str = "test-bus";
    const BUS_CLIENT_NAME: &'static str = "sandbox-service";

    fn create_env_with_netstack(sandbox: &SandboxProxy) -> Result<ManagedEnvironmentProxy, Error> {
        let (env, env_server_end) = fidl::endpoints::create_proxy::<ManagedEnvironmentMarker>()?;
        let services = vec![LaunchService {
            name: String::from("fuchsia.netstack.Netstack"),
            url: String::from("fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx"),
            arguments: None,
        }];
        sandbox.create_environment(
            env_server_end,
            &mut EnvironmentOptions {
                name: String::from(""), // don't care about the name, let it be created by itself
                services: services,
                devices: vec![],
                inherit_parent_launch_services: false,
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
    async fn launch_dummy_proc_test() {
        let sandbox = client::connect_to_service::<SandboxMarker>()
            .context("Can't connect to sandbox")
            .unwrap();

        let (return_code, reason) = await!(sandbox
            .run_test(
                &mut LaunchOptions {
                    package_url: String::from(DUMMY_PROC),
                    arguments: vec![],
                    cmx_override: None,
                },
                None,
            )
            .on_timeout(TEST_TIMEOUT.seconds().after_now(), || panic!("Test timed out")))
        .context("Can't run test")
        .unwrap();

        assert_eq!(reason, TerminationReason::Exited);
        assert_eq!(return_code, 0);
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn launch_dummy_proc_test_failure() {
        let sandbox = client::connect_to_service::<SandboxMarker>()
            .context("Can't connect to sandbox")
            .unwrap();

        let (return_code, reason) = await!(sandbox
            .run_test(
                &mut LaunchOptions {
                    package_url: String::from(DUMMY_PROC),
                    arguments: vec![String::from("-f")],
                    cmx_override: None,
                },
                None,
            )
            .on_timeout(TEST_TIMEOUT.seconds().after_now(), || panic!("Test timed out")))
        .context("Can't run test")
        .unwrap();

        assert_eq!(reason, TerminationReason::Exited);
        assert_ne!(return_code, 0);
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn launch_dummy_proc_cmx_override() {
        let sandbox = client::connect_to_service::<SandboxMarker>()
            .context("Can't connect to sandbox")
            .unwrap();

        // run a test whose cmx tells it to have a child environment
        // which spawns dummy_proc with "-f" flag, making if effectively fail.
        let (return_code, reason) = await!(sandbox
            .run_test(
                &mut LaunchOptions {
                    package_url: String::from(DUMMY_PROC),
                    arguments: vec![],
                    cmx_override: Some(String::from(
                        r#"
                    {
                       "environment" : {
                          "children": [{
                             "test" : [
                                { "arguments": ["-f"] }
                             ]
                          }]
                       }
                    }
                    "#
                    )),
                },
                None,
            )
            .on_timeout(TEST_TIMEOUT.seconds().after_now(), || panic!("Test timed out")))
        .context("Can't run test")
        .unwrap();

        // check that return is a failure, proving effectively that the cmx override worked.
        assert_eq!(reason, TerminationReason::Exited);
        assert_ne!(return_code, 0);
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn hop_on_test_environment() {
        let sandbox = client::connect_to_service::<SandboxMarker>()
            .context("Can't connect to sandbox")
            .unwrap();

        let (root_env, root_env_server_end) =
            fidl::endpoints::create_proxy::<ManagedEnvironmentMarker>().unwrap();

        // run dummy proc waiting for event code 1 with bus name "dummy_proc",
        // and get a reference to the root environment
        let fut = sandbox
            .run_test(
                &mut LaunchOptions {
                    package_url: String::from(DUMMY_PROC),
                    arguments: vec![
                        String::from("-e"),
                        String::from("1"),
                        String::from("-n"),
                        String::from("dummy_proc"),
                    ],
                    cmx_override: None,
                },
                Some(root_env_server_end),
            )
            .on_timeout(TEST_TIMEOUT.seconds().after_now(), || panic!("Test timed out"));

        // get bus service from root environment and connect to the dummy_proc bus (which is called
        // test-bus)

        let (sync_manager, sync_manager_server_end) =
            fidl::endpoints::create_proxy::<SyncManagerMarker>().unwrap();
        root_env
            .connect_to_service(SyncManagerMarker::NAME, sync_manager_server_end.into_channel())
            .context("Can't connect to SyncManager service")
            .unwrap();

        let (bus, bus_server_end) = fidl::endpoints::create_proxy::<BusMarker>().unwrap();
        sync_manager
            .bus_subscribe(TEST_BUS, BUS_CLIENT_NAME, bus_server_end)
            .context("Can't get on bus")
            .unwrap();

        // wait for the dummy_proc client to show up on the bus
        let (wait_ok, _) = await!(bus
            .wait_for_clients(&mut vec!["dummy_proc"].drain(..), TEST_TIMEOUT.seconds().nanos(),))
        .context("Can't wait for clients on bus")
        .unwrap();
        assert!(wait_ok, "timed out waiting for child on bus");

        // publish the event that dummy_proc is waiting for
        bus.publish(Event { code: Some(1), message: None, arguments: None })
            .context("Failed to publish event")
            .unwrap();

        let (return_code, reason) = await!(fut).context("Can't run test").unwrap();

        // check that return is a failure, proving effectively that the cmx override worked.
        assert_eq!(reason, TerminationReason::Exited);
        assert_eq!(return_code, 0);
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
        let env1 = create_env_with_netstack(&sandbox).unwrap();
        let env2 = create_env_with_netstack(&sandbox).unwrap();

        let _net1 = await!(create_network(&env1, "network")).unwrap();
        let net1_retrieve = await!(get_network(&env1, "network"));
        assert!(net1_retrieve.is_ok(), "can retrieve net1 from env1");
        let net2_retrieve = await!(get_network(&env2, "network"));
        assert!(net2_retrieve.is_err(), "net should not exist in env2");
    }
}
