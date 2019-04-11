// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{bail, Error, ResultExt},
    fdio,
    fidl_fuchsia_media_playback::*,
    fuchsia_component::client::connect_to_service,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::prelude::*,
    std::fs::File,
    structopt::StructOpt,
    url::Url,
};

#[derive(StructOpt, Debug)]
#[structopt(name = "audio_player_rust")]
struct Opt {
    #[structopt(
        parse(try_from_str),
        default_value = "https://storage.googleapis.com/fuchsia/assets/video/656a7250025525ae5a44b43d23c51e38b466d146"
    )]
    url: Url,
}

#[derive(Default, Debug)]
struct App {
    status_version: u64,
    metadata_displayed: bool,
}

impl App {
    pub fn new() -> Self {
        Default::default()
    }

    pub async fn run(&mut self) -> Result<(), Error> {
        let Opt { url } = Opt::from_args();

        let player =
            connect_to_service::<PlayerMarker>().context("Failed to connect to media player")?;
        if url.scheme() == "file" {
            let file = File::open(url.path())?;
            let handle = fdio::transfer_fd(file)?;
            player.set_file_source(zx::Channel::from(handle))?;
        } else {
            player.set_http_source(url.as_str(), None)?;
        }
        player.play()?;

        let mut player_event_stream = player.take_event_stream();
        if let Some(event) = await!(player_event_stream.try_next())? {
            let PlayerEvent::OnStatusChanged { player_status } = event;
            self.display_status(&player_status);
            Ok(())
        } else {
            bail!("No media player status change detected")
        }
    }

    fn display_status(&mut self, status: &PlayerStatus) {
        if let Some(ref metadata) = status.metadata {
            if self.metadata_displayed {
                return;
            }

            self.metadata_displayed = true;

            print!("Duration = {} ns\n", status.duration);

            for property in &metadata.properties {
                print!("{} = {}\n", property.label, property.value);
            }
        }
    }
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    executor.run_singlethreaded(App::new().run())
}
