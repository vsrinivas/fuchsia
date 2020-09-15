// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_netemul_network::{
        EndpointManagerMarker, NetworkContextMarker, NetworkManagerMarker,
    },
    fidl_fuchsia_netemul_sync::{BusMarker, BusProxy, Event, SyncManagerMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    futures::TryStreamExt,
    std::fs,
    std::path::Path,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
struct Opt {
    #[structopt(short = "t")]
    test: Option<i32>,
    #[structopt(short = "n", default_value = "root")]
    name: String,
}

const BUS_NAME: &str = "test-bus";
const NETWORK_NAME: &str = "test-net";
const EP0_NAME: &str = "ep0";
const EP1_NAME: &str = "ep1";
const EVENT_CODE: i32 = 1;
const SETUP_FILE: &str = "/data/test-setup";
const SETUP_FILE_DATA: &str = "Hello World";

pub struct BusConnection {
    bus: BusProxy,
}

impl BusConnection {
    pub fn new(client: &str) -> Result<BusConnection, Error> {
        let busm = client::connect_to_service::<SyncManagerMarker>()
            .context("SyncManager not available")?;
        let (bus, busch) = fidl::endpoints::create_proxy::<BusMarker>()?;
        busm.bus_subscribe(BUS_NAME, client, busch)?;
        Ok(BusConnection { bus })
    }

    pub fn publish_code(&self, code: i32) -> Result<(), Error> {
        self.bus.publish(Event { code: Some(code), message: None, arguments: None })?;
        Ok(())
    }

    pub async fn wait_for_client(&mut self, expect: &'static str) -> Result<(), Error> {
        let _ = self.bus.wait_for_clients(&mut vec![expect].drain(..), 0).await?;
        Ok(())
    }

    pub async fn wait_for_event(&mut self, code: i32) -> Result<(), Error> {
        let mut stream = self.bus.take_event_stream().try_filter_map(|event| match event {
            fidl_fuchsia_netemul_sync::BusEvent::OnBusData { data } => match data.code {
                Some(rcv_code) => {
                    if rcv_code == code {
                        futures::future::ok(Some(()))
                    } else {
                        futures::future::ok(None)
                    }
                }
                None => futures::future::ok(None),
            },
            _ => futures::future::ok(None),
        });
        stream.try_next().await?;
        Ok(())
    }
}

fn service_path(name: &str) -> String {
    format!("/svc/{}", name)
}

fn device_path(name: &str) -> String {
    format!("/vdev/{}", name)
}

fn check_path_present(path: &str) -> Result<(), Error> {
    if Path::new(path).exists() {
        Ok(())
    } else {
        Err(format_err!("Path {} not present, expected it to be there", path))
    }
}

fn check_path_absent(path: &str) -> Result<(), Error> {
    if Path::new(path).exists() {
        Err(format_err!("Path {} present, expected it to be absent.", path))
    } else {
        Ok(())
    }
}

fn check_netemul_environment() -> Result<(), Error> {
    let () = check_path_present(&service_path("fuchsia.netemul.sync.SyncManager"))?;
    let () = check_path_present(&service_path("fuchsia.netemul.environment.ManagedEnvironment"))?;
    let () = check_path_present(&service_path("fuchsia.netemul.network.NetworkContext"))?;
    Ok(())
}

async fn check_network() -> Result<(), Error> {
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    let (epm, epmch) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    let () = netctx.get_endpoint_manager(epmch)?;
    let (netm, netmch) = fidl::endpoints::create_proxy::<NetworkManagerMarker>()?;
    let () = netctx.get_network_manager(netmch)?;

    let net = netm.get_network(NETWORK_NAME).await?;
    if net == None {
        return Err(format_err!("Could not retrieve network {}.", NETWORK_NAME));
    }

    let ep0 = epm.get_endpoint(EP0_NAME).await?;
    if ep0 == None {
        return Err(format_err!("Could not retrieve endpoint {}", EP0_NAME));
    }

    let ep1 = epm.get_endpoint(EP1_NAME).await?;
    if ep1 == None {
        return Err(format_err!("Could not retrieve endpoint {}", EP1_NAME));
    }

    Ok(())
}

async fn root_wait_for_children(mut bus: BusConnection) -> Result<(), Error> {
    // wait for three hits on the bus, representing each child test
    for i in 0..3 {
        let () = bus.wait_for_event(EVENT_CODE).await?;
        log::info!("Got ping from child {}", i);
    }

    Ok(())
}

async fn child_publish_on_bus(mut bus: BusConnection) -> Result<(), Error> {
    // wait for root to show up on the bus...
    let () = bus.wait_for_client("root").await?;
    // ... then publish an event so root knows we were spawned
    let () = bus.publish_code(EVENT_CODE)?;
    Ok(())
}

fn run_root(opt: &Opt) -> Result<(), Error> {
    log::info!("Running main test: {}", opt.name);
    let () = check_netemul_environment()?;
    let () = check_path_present(&service_path("fuchsia.netstack.Netstack"))?;
    let () = check_path_present(&device_path("class/ethernet/ep0"))?;
    let () = check_path_present(&device_path("class/ethernet/ep1"))?;

    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    // check that network was created according to spec
    let () = executor.run_singlethreaded(check_network())?;

    // check that the setup process ran:
    let setup_data = fs::read_to_string(SETUP_FILE).context("Can't open setup file.")?;
    if setup_data != SETUP_FILE_DATA {
        return Err(format_err!("Setup file contents mismatch, got {}", setup_data));
    }

    // wait for children on bus
    let bus = BusConnection::new(&opt.name)?;
    executor.run_singlethreaded(root_wait_for_children(bus))
}

// environment 1 inherits from the root environment
fn run_test_1(opt: &Opt) -> Result<(), Error> {
    log::info!("Running test 1: {}", opt.name);
    let () = check_netemul_environment()?;
    let () = check_path_present(&service_path("fuchsia.netstack.Netstack"))?;
    let () = check_path_absent(&device_path("class/ethernet/ep0"))?;
    let () = check_path_absent(&device_path("class/ethernet/ep1"))?;

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let bus = BusConnection::new(&opt.name)?;
    executor.run_singlethreaded(child_publish_on_bus(bus))
}

// environment 2 does NOT inherit from the root environment
fn run_test_2(opt: &Opt) -> Result<(), Error> {
    log::info!("Running test 2: {}", opt.name);
    let () = check_netemul_environment()?;
    let () = check_path_absent(&service_path("fuchsia.netstack.Netstack"))?;
    let () = check_path_absent(&device_path("class/ethernet/ep0"))?;
    let () = check_path_absent(&device_path("class/ethernet/ep1"))?;

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let bus = BusConnection::new(&opt.name)?;
    executor.run_singlethreaded(child_publish_on_bus(bus))
}

fn run_setup_test(opt: &Opt) -> Result<(), Error> {
    log::info!("Running setup test: {}", opt.name);
    // create a file in /data, that will be verified by root test
    let () = fs::write(SETUP_FILE, SETUP_FILE_DATA).context("setup can't write file")?;
    Ok(())
}

fn main() -> Result<(), Error> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;

    let opt = Opt::from_args();
    match opt.test {
        None => run_root(&opt),
        Some(1) => run_test_1(&opt),
        Some(2) => run_test_2(&opt),
        Some(3) => run_setup_test(&opt),
        _ => Err(format_err!("Unrecognized test option")),
    }
}
