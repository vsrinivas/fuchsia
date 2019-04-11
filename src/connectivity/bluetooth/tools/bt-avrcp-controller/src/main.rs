// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_bluetooth_avrcp::{
        AvrcpMarker, ControllerEvent, ControllerEventStream, ControllerMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    futures::{select, FutureExt, TryStreamExt},
    pin_utils::pin_mut,
    structopt::StructOpt,
};

/// Define the command line arguments that the tool accepts.
#[derive(StructOpt)]
#[structopt(
    version = "0.2.0",
    author = "Fuchsia Bluetooth Team",
    about = "Bluetooth AVRCP Controller CLI"
)]
struct Opt {
    #[structopt(help = "Target Device ID")]
    device: String,
}

async fn controller_listener(mut stream: ControllerEventStream) -> Result<(), Error> {
    while let Some(evt) = await!(stream.try_next())? {
        match evt {
            ControllerEvent::PlaybackStatusChanged { status } => {
                println!("Event: PlaybackStatusChanged(status = {:?})", status);
            }
            ControllerEvent::TrackChanged { track_id } => {
                println!("Event: TrackChanged(track_id = {:?})", track_id);
            }
            ControllerEvent::TrackReachedStart {} => {
                println!("Event: TrackReachedStart()");
            }
            ControllerEvent::TrackReachedEnd {} => {
                println!("Event: TrackReachedEnd()");
            }
            ControllerEvent::TrackPosChanged { pos } => {
                println!("Event: TrackPosChanged(pos = {:?})", pos);
            }
            ControllerEvent::BattStatusChanged { battery_status } => {
                println!("Event: BattStatusChanged(battery_status = {:?})", battery_status);
            }
            ControllerEvent::SystemStatusChanged { system_status } => {
                println!("Event: SystemStatusChanged(system_status = {:?})", system_status);
            }
            ControllerEvent::PlayerApplicationSettingsChanged { application_settings } => {
                println!(
                    "Event: PlayerApplicationSettingsChanged(application_settings = {:?})",
                    application_settings
                );
            }
            ControllerEvent::AddressedPlayerChanged { player_id } => {
                println!("Event: AddressedPlayerChanged(player_id = {:?})", player_id);
            }
            ControllerEvent::VolumeChanged { volume } => {
                println!("Event: VolumeChanged(volume = {:?})", volume);
            }
        }
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    eprintln!("Connecting to \"{device}\" AVRCP remote target service.", device = opt.device);

    let avrcp_svc = connect_to_service::<AvrcpMarker>()
        .context("failed to connect to Bluetooth AVRCP interface")?;

    // Create a channel for our Request<Controller> to live
    let (c_client, c_server) =
        create_endpoints::<ControllerMarker>().expect("error creating Controller endpoint");

    await!(avrcp_svc.get_controller_for_target(opt.device.as_str(), c_server))?;

    let controller = c_client.into_proxy().expect("error obtaining controller client proxy");

    let evt_stream = controller.clone().take_event_stream();
    let event_fut = controller_listener(evt_stream).fuse();

    pin_mut!(event_fut);
    // These futures should only return when something fails.
    select! {
        _ = event_fut => (),
        // TODO: add repl future
    }
    Ok(())
}
