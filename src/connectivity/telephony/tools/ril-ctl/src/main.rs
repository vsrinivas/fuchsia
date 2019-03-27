// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! ril-ctl is used for interacting with devices that expose the standard
//! Fuchsia RIL (FRIL)
//!
//! Ex: ril-ctl
//!
//! or
//!
//! Ex: ril-ctl -d /dev/class/qmi-usb-transport/000
//!

#![feature(async_await, await_macro, futures_api)]

use {
    crate::commands::{Cmd, ReplControl},
    failure::{format_err, Error, ResultExt},
    fidl::endpoints,
    fidl_fuchsia_net::{IPv4Address, IpAddress, Subnet},
    fidl_fuchsia_net_stack::{
        ForwardingDestination, ForwardingEntry, InterfaceAddress, StackMarker,
    },
    fidl_fuchsia_netstack::NetstackMarker,
    fidl_fuchsia_telephony_manager::ManagerMarker,
    fidl_fuchsia_telephony_ril::{
        RadioInterfaceLayerMarker, RadioInterfaceLayerProxy, RadioPowerState, *,
    },
    fuchsia_app::{
        client::{connect_to_service, Launcher},
        fuchsia_single_component_package_url,
    },
    fuchsia_async::{self as fasync, futures::select},
    futures::{FutureExt, TryFutureExt},
    parking_lot::Mutex,
    pin_utils::pin_mut,
    qmi,
    std::{fs::File, path::PathBuf, sync::Arc},
    structopt::StructOpt,
};

mod commands;
mod repl;

static PROMPT: &str = "\x1b[35mril>\x1b[0m ";
const RIL_URI: &str = fuchsia_single_component_package_url!("ril-qmi");

// TODO count actual bit number
fn u32_to_cidr(ip: u32) -> Result<u8, Error> {
    match ip & 0xFF {
        255 => Ok(32),
        254 => Ok(31),
        252 => Ok(30),
        248 => Ok(29),
        240 => Ok(28),
        224 => Ok(27),
        192 => Ok(26),
        128 => Ok(25),
        0 => Ok(24),
        e => Err(format_err!("bad u32 to cidr conversion: {}", e)),
    }
}

fn u32_to_ip_str(ip: u32) -> String {
    format!(
        "{}.{}.{}.{}",
        ((ip >> 24) & 0xFF),
        ((ip >> 16) & 0xFF),
        ((ip >> 8) & 0xFF),
        (ip & 0xFF)
    )
}

// only supports ipv4 now
fn u32_to_netaddr(ip: u32, mask: u32) -> Result<InterfaceAddress, Error> {
    let cidr = u32_to_cidr(mask)?;
    Ok(InterfaceAddress {
        ip_address: IpAddress::Ipv4(IPv4Address {
            addr: [
                ((ip >> 24) & 0xFF) as u8,
                ((ip >> 16) & 0xFF) as u8,
                ((ip >> 8) & 0xFF) as u8,
                (ip & 0xFF) as u8,
            ],
        }),
        prefix_len: cidr,
    })
}

async fn get_imei<'a>(
    _args: &'a [&'a str],
    ril_modem: &'a RadioInterfaceLayerProxy,
) -> Result<String, Error> {
    match await!(ril_modem.get_device_identity())? {
        GetDeviceIdentityReturn::Imei(string) => Ok(string),
        GetDeviceIdentityReturn::Error(_state) => Err(format_err!("error")),
    }
}

async fn connect<'a>(
    args: &'a [&'a str],
    ril_modem: &'a RadioInterfaceLayerProxy,
) -> Result<(NetworkSettings, NetworkConnectionProxy), Error> {
    match await!(ril_modem.start_network(args[0]))? {
        StartNetworkReturn::Conn(iface) => {
            let settings = await!(ril_modem.get_network_settings())?;
            if let GetNetworkSettingsReturn::Settings(settings) = settings {
                return Ok((settings, iface.into_proxy()?));
            }
            Err(format_err!("error"))
        }
        StartNetworkReturn::Error(_e) => Err(format_err!("error")),
    }
}

async fn get_power<'a>(
    _args: &'a [&'a str],
    ril_modem: &'a RadioInterfaceLayerProxy,
) -> Result<String, Error> {
    match await!(ril_modem.radio_power_status())? {
        RadioPowerStatusReturn::Result(state) => match state {
            RadioPowerState::On => Ok(String::from("radio on")),
            RadioPowerState::Off => Ok(String::from("radio off")),
        },
        RadioPowerStatusReturn::Error(_e) => Err(format_err!("error")),
    }
}

pub struct Connections {
    pub net_conn: Option<NetworkConnectionProxy>,
    pub file_ref: Option<File>,
}

async fn handle_cmd<'a>(
    ril_modem: &'a RadioInterfaceLayerProxy,
    line: String,
    state: Arc<Mutex<Connections>>,
) -> Result<ReplControl, Error> {
    let components: Vec<_> = line.trim().split_whitespace().collect();
    if let Some((raw_cmd, args)) = components.split_first() {
        let cmd = raw_cmd.parse();
        let res = match cmd {
            Ok(Cmd::Connect) => {
                let (settings, iface) = await!(connect(args, &ril_modem))?;
                {
                    state.lock().net_conn = Some(iface);
                }
                eprintln!("IP Addr: {}", u32_to_ip_str(settings.ip_v4_addr));
                eprintln!("IP Subnet: {}", u32_to_ip_str(settings.ip_v4_subnet));
                eprintln!("IP Gateway: {}", u32_to_ip_str(settings.ip_v4_gateway));
                eprintln!("IP DNS: {}", u32_to_ip_str(settings.ip_v4_dns));
                match state.lock().file_ref {
                    Some(ref file_ref) => {
                        // Set up the netstack.
                        // TODO not hardcode to iface 3
                        await!(qmi::set_network_status(file_ref, true))?;
                        let netstack = connect_to_service::<StackMarker>()?;
                        let old_netstack = connect_to_service::<NetstackMarker>()?;
                        await!(old_netstack.set_dhcp_client_status(3, false))?;
                        await!(netstack.add_interface_address(3, &mut u32_to_netaddr(settings.ip_v4_addr, settings.ip_v4_subnet)?))?;
                        let ip = settings.ip_v4_addr;
                        await!(netstack.add_forwarding_entry(&mut ForwardingEntry {
                            destination: ForwardingDestination::NextHop(IpAddress::Ipv4(IPv4Address{
                                addr: [
                                    ((settings.ip_v4_gateway >> 24) & 0xFF) as u8,
                                    ((settings.ip_v4_gateway >> 16) & 0xFF) as u8,
                                    ((settings.ip_v4_gateway >> 8)  & 0xFF) as u8,
                                    (settings.ip_v4_gateway & 0xFF) as u8]})),
                            subnet: Subnet {
                                addr: IpAddress::Ipv4(IPv4Address{
                                    addr: [
                                        ((ip >> 24) & 0xFF) as u8,
                                        ((ip >> 16) & 0xFF) as u8,
                                        ((ip >> 8)  & 0xFF) as u8,
                                        (ip & 0xFF) as u8]}),
                                prefix_len: u32_to_cidr(settings.ip_v4_subnet)?
                            },
                        }))?;
                        Ok("connected".to_string())
                    }
                    None => Ok("set up connection on radio. Did not configure ethernet device, exclusive access required".to_string())
                }
            }
            Ok(Cmd::PowerStatus) => await!(get_power(args, &ril_modem)),
            Ok(Cmd::Imei) => await!(get_imei(args, &ril_modem)),
            Ok(Cmd::Help) => Ok(Cmd::help_msg().to_string()),
            Ok(Cmd::Exit) | Ok(Cmd::Quit) => return Ok(ReplControl::Break),
            Err(_) => Ok(format!("\"{}\" is not a valid command", raw_cmd)),
        }?;
        if res != "" {
            println!("{}", res);
        }
    }

    Ok(ReplControl::Continue)
}

/// A basic example
#[derive(StructOpt, Debug)]
#[structopt(name = "basic")]
struct Opt {
    /// Device path (e.g. /dev/class/qmi-transport/000)
    #[structopt(short = "d", long = "device", parse(from_os_str))]
    device: Option<PathBuf>,
}

pub fn main() -> Result<(), Error> {
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let args = Opt::from_args();

    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let file = match args.device {
        Some(ref device) => Some(File::open(device)?),
        None => None,
    };
    let conns = Arc::new(Mutex::new(Connections { file_ref: file, net_conn: None }));

    let fut = async move {
        let app; // need outside the match so it won't drop
        let telephony_svc;
        let (ril, server) = endpoints::create_proxy()?;
        let ril_modem = match args.device {
            Some(device) => {
                eprintln!("Connecting with exclusive access to {}..", device.display());
                let file = File::open(device)?;
                let chan = await!(qmi::connect_transport_device(&file))?;
                app = launcher
                    .launch(RIL_URI.to_string(), None)
                    .context("Failed to launch ril-qmi service")?;
                let ril_modem = app.connect_to_service(RadioInterfaceLayerMarker)?;
                let resp = await!(ril_modem.connect_transport(chan))?;
                if !resp {
                    return Err(format_err!(
                        "Failed to connect the driver to the RIL (check telephony svc is not running?)"
                    ));
                }
                Ok::<_, Error>(ril_modem)
            }
            None => {
                eprintln!("Connecting through telephony service...");
                telephony_svc = connect_to_service::<ManagerMarker>()?;
                let resp = await!(telephony_svc.get_ril_handle(server))?;
                if !resp {
                    return Err(format_err!("Failed to get an active RIL"));
                }
                Ok::<_, Error>(ril)
            }
        }?;

        let repl = repl::run(&ril_modem, conns)
            .unwrap_or_else(|e| eprintln!("REPL failed unexpectedly {:?}", e));
        pin_mut!(repl);
        select! {
            () = repl.fuse() => Ok(()),
            // TODO(bwb): events loop future
        }
    };

    exec.run_singlethreaded(fut)
}
