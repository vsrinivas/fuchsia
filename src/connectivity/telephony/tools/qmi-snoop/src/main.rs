// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! qmi-snoop is used for snooping Qmi messages sent/received by transport driver

#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_telephony_snoop::{
        Message as SnoopMessage, PublisherMarker as QmiSnoopMarker,
        PublisherRequest as QmiSnoopRequest, PublisherRequestStream as QmiSnoopRequestStream,
    },
    fuchsia_async as fasync, futures,
    futures::stream::TryStreamExt,
    qmi,
    std::fs::File,
    std::path::PathBuf,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
#[structopt(name = "qmi-snoop")]
struct Opt {
    /// Device path (e.g. /dev/class/qmi-transport/000)
    #[structopt(short = "d", long = "device", parse(from_os_str))]
    device: Option<PathBuf>,
}

pub fn main() -> Result<(), Error> {
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let args = Opt::from_args();
    let device = match args.device {
        Some(device_path) => device_path,
        None => PathBuf::from("/dev/class/qmi-transport/000"),
    };
    let fut = async move {
        eprintln!("Connecting with exclusive access to {}..", device.display());
        let file: File = File::open(device)?;
        let snoop_endpoint_server_side: ServerEnd<QmiSnoopMarker> =
            await!(qmi::connect_snoop_channel(&file))?;
        let mut request_stream: QmiSnoopRequestStream = snoop_endpoint_server_side.into_stream()?;
        while let Ok(Some(QmiSnoopRequest::SendMessage { msg, control_handle: _ })) =
            await!(request_stream.try_next())
        {
            let qmi_message = match msg {
                SnoopMessage::QmiMessage(m) => Some(m),
            };
            match qmi_message {
                Some(message) => {
                    let slice = &message.opaque_bytes;
                    eprint!(
                        "Received msg direction: {:?}, timestamp: {}, msg:",
                        message.direction, message.timestamp
                    );
                    for element in slice.iter() {
                        eprint!(" {}", element);
                    }
                    eprint!("\n");
                }
                None => {}
            }
        }
        eprintln!("unexpected msg received");
        Ok::<_, Error>(())
    };
    exec.run_singlethreaded(fut)
}
