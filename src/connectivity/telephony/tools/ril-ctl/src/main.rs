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

use {
    crate::commands::{Cmd, ReplControl},
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Subnet},
    fidl_fuchsia_net_stack::{
        ForwardingDestination, ForwardingEntry, InterfaceAddress, StackMarker,
    },
    fidl_fuchsia_net_stack_ext::FidlReturn,
    fidl_fuchsia_netstack::NetstackMarker,
    fidl_fuchsia_telephony_manager::ManagerMarker,
    fidl_fuchsia_telephony_ril::{
        RadioInterfaceLayerMarker, RadioInterfaceLayerProxy, RadioPowerState, SetupMarker, *,
    },
    fuchsia_async::{self as fasync, futures::select},
    fuchsia_component::{
        client::{connect_to_service, launch, launcher},
        fuchsia_single_component_package_url,
    },
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
        ip_address: IpAddress::Ipv4(Ipv4Address {
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
    match ril_modem.get_device_identity().await? {
        Ok(imei) => Ok(imei),
        Err(_state) => Err(format_err!("error")),
    }
}

async fn connect<'a>(
    args: &'a [&'a str],
    ril_modem: &'a RadioInterfaceLayerProxy,
) -> Result<(NetworkSettings, NetworkConnectionProxy), Error> {
    match ril_modem.start_network(args[0]).await? {
        Ok(iface) => {
            let settings = ril_modem.get_network_settings().await?;
            if let Ok(settings) = settings {
                return Ok((settings, iface.into_proxy()?));
            }
            Err(format_err!("error"))
        }
        Err(_e) => Err(format_err!("error")),
    }
}

async fn get_power<'a>(
    _args: &'a [&'a str],
    ril_modem: &'a RadioInterfaceLayerProxy,
) -> Result<String, Error> {
    match ril_modem.radio_power_status().await? {
        Ok(state) => match state {
            RadioPowerState::On => Ok(String::from("radio on")),
            RadioPowerState::Off => Ok(String::from("radio off")),
        },
        Err(_e) => Err(format_err!("error")),
    }
}

async fn get_signal<'a>(
    _args: &'a [&'a str],
    ril_modem: &'a RadioInterfaceLayerProxy,
) -> Result<String, Error> {
    match ril_modem.get_signal_strength().await? {
        Ok(strength) => Ok(format!("{} dBm", strength)),
        Err(_e) => Err(format_err!("error")),
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
                let (settings, iface) = connect(args, &ril_modem).await?;
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
                        qmi::set_network_status(file_ref, true).await?;
                        let netstack = connect_to_service::<StackMarker>()?;
                        let old_netstack = connect_to_service::<NetstackMarker>()?;
                        let (client, server_end) = fidl::endpoints::create_proxy::<fidl_fuchsia_net_dhcp::ClientMarker>()?;
                        old_netstack.get_dhcp_client(3, server_end).await?.map_err(fuchsia_zircon::Status::from_raw)?;
                        client.stop().await?.map_err(fuchsia_zircon::Status::from_raw)?;

                        let () = netstack
                            .add_interface_address(
                                3,
                                &mut u32_to_netaddr(settings.ip_v4_addr, settings.ip_v4_subnet)?
                            )
                            .await
                            .squash_result()?;
                        let ip = settings.ip_v4_addr;
                        let () = netstack.add_forwarding_entry(&mut ForwardingEntry {
                            destination: ForwardingDestination::NextHop(IpAddress::Ipv4(Ipv4Address{
                                addr: [
                                    ((settings.ip_v4_gateway >> 24) & 0xFF) as u8,
                                    ((settings.ip_v4_gateway >> 16) & 0xFF) as u8,
                                    ((settings.ip_v4_gateway >> 8)  & 0xFF) as u8,
                                    (settings.ip_v4_gateway & 0xFF) as u8]})),
                            subnet: Subnet {
                                addr: IpAddress::Ipv4(Ipv4Address{
                                    addr: [
                                        ((ip >> 24) & 0xFF) as u8,
                                        ((ip >> 16) & 0xFF) as u8,
                                        ((ip >> 8)  & 0xFF) as u8,
                                        (ip & 0xFF) as u8]}),
                                prefix_len: u32_to_cidr(settings.ip_v4_subnet)?
                            },
                        }).await.squash_result()?;
                        Ok("connected".to_string())
                    }
                    None => Ok("set up connection on radio. Did not configure ethernet device, exclusive access required".to_string())
                }
            }
            Ok(Cmd::PowerStatus) => get_power(args, &ril_modem).await,
            Ok(Cmd::SignalStrength) => get_signal(args, &ril_modem).await,
            Ok(Cmd::Imei) => get_imei(args, &ril_modem).await,
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

    let launcher = launcher().context("Failed to open launcher service")?;
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
                let chan = qmi::connect_transport_device(&file).await?;
                app = launch(&launcher, RIL_URI.to_string(), None)
                    .context("Failed to launch ril-qmi service")?;
                let ril_modem_setup = app.connect_to_service::<SetupMarker>()?;
                let resp = ril_modem_setup.connect_transport(chan).await?;
                let ril_modem = app.connect_to_service::<RadioInterfaceLayerMarker>()?;
                if resp.is_err() {
                    return Err(format_err!(
                        "Failed to connect the driver to the RIL (check telephony svc is not running?)"
                    ));
                }
                Ok::<_, Error>(ril_modem)
            }
            None => {
                eprintln!("Connecting through telephony service...");
                telephony_svc = connect_to_service::<ManagerMarker>()?;
                let resp = telephony_svc.get_ril_handle(server).await?;
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
