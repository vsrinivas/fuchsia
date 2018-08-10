// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate failure;
extern crate fdio;
extern crate fidl;
extern crate fidl_fuchsia_mediaplayer;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate mxruntime_sys;
#[macro_use]
extern crate structopt;
extern crate url;

use app::client::connect_to_service;
use failure::{Error, ResultExt};
use fdio::fdio_sys::*;
use fidl_fuchsia_mediaplayer::*;
use futures::prelude::*;
use mxruntime_sys::*;
use std::fs::File;
use std::io;
use std::mem;
use std::os::unix::io::IntoRawFd;
use structopt::StructOpt;
use url::Url;
use zx::*;
use zx::sys::zx_handle_t;

fn channel_from_file(file: File) -> Result<zx::Channel, io::Error> {
    unsafe {
        let mut handles: [zx_handle_t; FDIO_MAX_HANDLES as usize] = mem::uninitialized();
        let mut types: [u32; FDIO_MAX_HANDLES as usize] = mem::uninitialized();
        let status = fdio_transfer_fd(file.into_raw_fd(), 0, handles.as_mut_ptr(), types.as_mut_ptr());
        if status < 0 {
            return Err(zx::Status::from_raw(status).into_io_error());
        } else if status != 1 {
            // status >0 indicates number of handles returned
            for i in 0..status as usize { drop(Handle::from_raw(handles[i])) };
            return Err(io::Error::new(io::ErrorKind::Other, "unexpected handle count"));
        }

        let handle = Handle::from_raw(handles[0]);

        if types[0] != PA_FDIO_REMOTE {
            return Err(io::Error::new(io::ErrorKind::Other, "unexpected handle type"));
        }

        Ok(Channel::from(handle))
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

    pub fn run(&mut self) -> Result<(), Error> {
        let mut executor = async::Executor::new().context("Error creating executor")?;

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

        let mut event_listener = player.take_event_stream().filter(move |event| {
            match event {
                Ok(MediaPlayerEvent::StatusChanged { status }) => {
                    self.display_status(&status);

                    futures::future::ready(status.end_of_stream)
                }
                _ => futures::future::ready(false)
            }
        });

        executor.run_singlethreaded(event_listener.next())
            .unwrap()
            .map(|_| ())
            .map_err(Into::into)
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

fn main() {
    let mut app = App::new();
    if let Err(e) = app.run() {
        println!("Error: {:?}", e);
    }
}
