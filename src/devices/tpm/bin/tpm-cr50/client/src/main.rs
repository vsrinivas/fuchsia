// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use argh::FromArgs;
use fidl_fuchsia_tpm_cr50::{
    Cr50Marker, Cr50Proxy, Cr50Rc, Cr50Status, PhysicalPresenceEvent,
    PhysicalPresenceNotifierEvent, PhysicalPresenceNotifierProxy, PhysicalPresenceState, WpState,
};
use fuchsia_zircon as zx;
use futures::TryStreamExt;

#[derive(FromArgs, PartialEq, Debug)]
/// A tool to interact with the Cr50 TPM.
struct Args {
    #[argh(subcommand)]
    cmd: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommand {
    Ccd(CcdSubCommand),
    Wp(WpCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "ccd")]
/// interact with case-closed debugging features.
struct CcdSubCommand {
    #[argh(subcommand)]
    cmd: CcdCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
/// command to use.
enum CcdCommand {
    GetInfo(GetInfo),
    Lock(CcdLock),
    Unlock(CcdUnlock),
    Open(CcdOpen),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-info")]
/// get info about CCD.
struct GetInfo {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "lock")]
/// lock CCD.
struct CcdLock {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "open")]
/// open CCD.
struct CcdOpen {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "unlock")]
/// unlock CCD.
struct CcdUnlock {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "wp")]
/// get the current write protect state.
struct WpCommand {}

#[fuchsia::component]
async fn main() {
    let args: Args = argh::from_env();
    run_cmd(args).await.unwrap_or_else(|e| {
        println!("Error while running command: {:?}", e);
    });
}

async fn handle_pp(client: PhysicalPresenceNotifierProxy) -> Result<(), Error> {
    let mut started = false;
    while let Some(event) =
        client.take_event_stream().try_next().await.context("Getting next event")?
    {
        match event {
            PhysicalPresenceNotifierEvent::OnChange { event } => match event {
                PhysicalPresenceEvent::State(PhysicalPresenceState::Done) => {
                    println!("Success!");
                    return Ok(());
                }
                PhysicalPresenceEvent::State(PhysicalPresenceState::Closed) => {
                    if started {
                        println!("Timed out, try again.");
                    } else {
                        println!("Done");
                    }
                    return Ok(());
                }
                PhysicalPresenceEvent::State(PhysicalPresenceState::AwaitingPress) => {
                    println!("Press the power button NOW!")
                }
                PhysicalPresenceEvent::State(PhysicalPresenceState::BetweenPresses) => {
                    println!("Waiting - another press will be needed soon...")
                }
                PhysicalPresenceEvent::Err(_) => {
                    println!("Internal error while polling for physical presence, try again?")
                }
                _ => unimplemented!(),
            },
        }
        started = true;
    }

    Ok(())
}

async fn run_ccd(proxy: Cr50Proxy, cmd: CcdCommand) -> Result<(), Error> {
    match cmd {
        CcdCommand::GetInfo(_) => {
            let (rc, info) = proxy
                .ccd_get_info()
                .await
                .context("Getting info (Sending FIDL request)")?
                .map_err(zx::Status::from_raw)
                .context("Getting info (Server-side failure)")?;
            if let Some(info) = info {
                println!("CCD state: {:?}", info.state);
                println!("CCD force disabled: {}", info.force_disabled);
                println!("CCD flags: {:?}", info.flags);
                println!("CCD indicator: {:?}", info.indicator);
                println!("Capabilities:");
                println!("{:^32} {:^16} {:^16}", "CAPABILITY", "CURRENT STATE", "(DEFAULT STATE)");
                for cap in info.capabilities.iter() {
                    print!(
                        "{:^32} {:^16}",
                        format!("{:?}", cap.capability),
                        format!("{:?}", cap.current_state)
                    );
                    if cap.current_state != cap.default_state {
                        println!(" {:^16}", format!("({:?})", cap.default_state));
                    } else {
                        println!();
                    }
                }
            } else {
                println!("Error: {:?}", rc);
            }
        }
        CcdCommand::Lock(_) => {
            let rc = proxy
                .ccd_lock()
                .await
                .context("Locking CCD (Sending FIDL request)")?
                .map_err(zx::Status::from_raw)
                .context("Locking CCD (Server-side failure)")?;
            if rc == Cr50Rc::Cr50(Cr50Status::Success) {
                println!("CCD locked");
            } else {
                println!("Failed to lock CCD: {:?}", rc);
            }
        }
        CcdCommand::Open(_) => {
            let (rc, client) = proxy
                .ccd_open(None)
                .await
                .context("Opening CCD (Sending FIDL request)")?
                .map_err(zx::Status::from_raw)
                .context("Opening CCD (Server-side failure)")?;

            match rc {
                Cr50Rc::Cr50(Cr50Status::Success) | Cr50Rc::Cr50(Cr50Status::InProgress) => {
                    handle_pp(client.unwrap().into_proxy().context("Making proxy")?)
                        .await
                        .context("Handling PP")?;
                }
                err => println!("Failed to open: {:?}", err),
            }
        }
        CcdCommand::Unlock(_) => {
            let (rc, client) = proxy
                .ccd_unlock(None)
                .await
                .context("Unlocking CCD (Sending FIDL request)")?
                .map_err(zx::Status::from_raw)
                .context("Unlocking CCD (Server-side failure)")?;

            match rc {
                Cr50Rc::Cr50(Cr50Status::Success) | Cr50Rc::Cr50(Cr50Status::InProgress) => {
                    handle_pp(client.unwrap().into_proxy().context("Making proxy")?)
                        .await
                        .context("Handling PP")?
                }
                err => println!("Failed to unlock: {:?}", err),
            }
        }
    };

    Ok(())
}

async fn run_cmd(args: Args) -> Result<(), Error> {
    let proxy = fuchsia_component::client::connect_to_protocol::<Cr50Marker>()
        .context("Connecting to firmware parameter service")?;
    match args.cmd {
        SubCommand::Ccd(CcdSubCommand { cmd }) => run_ccd(proxy, cmd).await?,
        SubCommand::Wp(_) => {
            let (rc, state) = proxy
                .wp_get_state()
                .await
                .context("Getting state")?
                .map_err(zx::Status::from_raw)
                .context("Getting state")?;
            if rc != Cr50Rc::Cr50(Cr50Status::Success) {
                println!("Error: {:?}", rc);
            } else {
                print!("WP state: ");
                if state.contains(WpState::Force) {
                    print!("force ");
                }
                if state.contains(WpState::Enable) {
                    println!("enabled");
                } else {
                    println!("disabled");
                }
                print!("At boot:  ");
                if state.contains(WpState::AtBootSet) {
                    print!("force ");
                    if state.contains(WpState::AtBootEnable) {
                        println!("enable");
                    } else {
                        println!("disable");
                    }
                } else {
                    println!("follow_batt_pres");
                }
            }
        }
    };

    Ok(())
}
