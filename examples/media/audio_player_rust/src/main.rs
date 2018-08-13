// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{bail, Error, ResultExt},
    fdio::fdio_sys::*,
    fidl_fuchsia_mediaplayer::*,
    fuchsia_app::client::connect_to_service,
    fuchsia_async as fasync,
    fuchsia_zircon::{
        self as zx,
        sys::zx_handle_t,
    },
    futures::prelude::*,
    mxruntime_sys::*,
    std::{
        fs::File,
        io,
        mem,
        os::unix::io::IntoRawFd,
    },
    structopt::StructOpt,
    url::Url,
};

fn channel_from_file(file: File) -> Result<zx::Channel, io::Error> {
    unsafe {
        let mut handles: [zx_handle_t; FDIO_MAX_HANDLES as usize] = mem::uninitialized();
        let mut types: [u32; FDIO_MAX_HANDLES as usize] = mem::uninitialized();
        let status = fdio_transfer_fd(file.into_raw_fd(), 0, handles.as_mut_ptr(), types.as_mut_ptr());
        if status < 0 {
            return Err(zx::Status::from_raw(status).into_io_error());
        } else if status != 1 {
            // status >0 indicates number of handles returned
            for i in 0..status as usize { drop(zx::Handle::from_raw(handles[i])) };
            return Err(io::Error::new(io::ErrorKind::Other, "unexpected handle count"));
        }

        let handle = zx::Handle::from_raw(handles[0]);

        if types[0] != PA_FDIO_REMOTE {
            return Err(io::Error::new(io::ErrorKind::Other, "unexpected handle type"));
        }

        Ok(zx::Channel::from(handle))
    }
}

#[derive(StructOpt, Debug)]
#[structopt(name = "audio_player_rust")]
struct Opt {
    #[structopt(parse(try_from_str),
    default_value = "https://storage.googleapis.com/fuchsia/assets/video/656a7250025525ae5a44b43d23c51e38b466d146")]
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

        let player = connect_to_service::<MediaPlayerMarker>().context("Failed to connect to media player")?;
        if url.scheme() == "file" {
            let file = File::open(url.path())?;
            let channel = channel_from_file(file)?;
            player.set_file_source(channel)?;
        } else {
            player.set_http_source(url.as_str())?;
        }
        player.play()?;

        let mut player_event_stream = player.take_event_stream();
        if let Some(event) = await!(player_event_stream.try_next())? {
            let MediaPlayerEvent::StatusChanged { status } = event;
            self.display_status(&status);
            Ok(())
        } else {
            bail!("No media player status change detected")
        }
    }

    fn display_status(&mut self, status: &MediaPlayerStatus) {
        if let Some(ref metadata) = status.metadata {
            if self.metadata_displayed {
                return;
            }

            self.metadata_displayed = true;

            print!("Duration = {} ns\n", status.duration_ns);

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
