// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(
    async_await,
    await_macro,
    futures_api,
    pin,
    arbitrary_self_types
)]
#![deny(warnings)]

use failure::format_err;
use std::fs::{File, OpenOptions};
use std::io;
use std::path::Path;
use std::str::FromStr;

use clap::{App, Arg};
use failure::Error;
use fuchsia_async::Executor;
use futures::StreamExt;

use fuchsia_bluetooth::hci;
use fuchsia_zircon::Channel;

use crate::snooper::*;
mod snooper;

static HCI_DEVICE: &'static str = "/dev/class/bt-hci/000";

fn start<W: io::Write>(
    device_path: &Path, mut out: W, format: Format, count: Option<u64>,
) -> Result<(), Error> {
    let hci_device = OpenOptions::new()
        .read(true)
        .write(true)
        .open(device_path)?;
    let mut exec = Executor::new().unwrap();
    let mut snooper = Snooper::new(Channel::from(hci::open_snoop_channel(&hci_device)?));

    let success = match format {
        Format::Btsnoop => out.write(Snooper::btsnoop_header().as_slice()),
        Format::Pcap => out.write(Snooper::pcap_header().as_slice()),
    };

    if success.is_err() {
        return Err(format_err!("failed to write header"));
    }
    let _ = out.flush();

    let fut = async {
        let mut pkt_cnt = 0u64;
        while let Some(pkt) = await!(snooper.next()) {
            if let Some(count) = count {
                if pkt_cnt >= count {
                    return Ok(());
                }
                pkt_cnt += 1;
            }
            let wrote = match format {
                Format::Btsnoop => out.write(pkt.to_btsnoop_fmt().as_slice()),
                Format::Pcap => out.write(pkt.to_pcap_fmt().as_slice()),
            };
            match wrote {
                Ok(_) => {
                    let _ = out.flush();
                }
                Err(_) => return Err(format_err!("failed to write packet to file")),
            }
        }
        Ok(())
    };

    exec.run_singlethreaded(fut)
}

fn main() {
    let args = App::new("btsnoop")
        .version("0.1.0")
        .about("Snooping on your bluetooth packets :)")
        .author("Fuchsia Bluetooth Team")
        .arg(
            Arg::with_name("device")
                .short("d")
                .long("device")
                .value_name("DEVICE")
                .help("path to bt-hci device")
                .takes_value(true),
        ).arg(
            Arg::with_name("format")
                .short("f")
                .long("format")
                .value_name("FORMAT")
                .help("file format. options: [btsnoop, pcap]")
                .takes_value(true),
        ).arg(
            Arg::with_name("count")
                .short("c")
                .long("count")
                .value_name("COUNT")
                .help("number of packets to record. Default: infinite")
                .takes_value(true),
        ).arg(
            Arg::with_name("output")
                .short("o")
                .long("file")
                .value_name("FILE")
                .help("output location (default is stdout)")
                .takes_value(true),
        ).get_matches();

    let device_path = Path::new(args.value_of("device").unwrap_or(HCI_DEVICE));
    let count = match args.value_of("count") {
        Some(num) => Some(u64::from_str(num).unwrap()),
        None => None,
    };
    match count {
        Some(count) => {
            eprintln!(
                "btsnoop: Capturing {} packets on bt-hci/{}",
                count,
                device_path.file_name().unwrap().to_string_lossy()
            );
        }
        None => {
            eprintln!(
                "btsnoop: Capturing packets on bt-hci/{}",
                device_path.file_name().unwrap().to_string_lossy()
            );
        }
    };

    let format = match args.value_of("format") {
        Some(val) => {
            if val == "pcap" {
                Format::Pcap
            } else if val == "btsnoop" {
                Format::Btsnoop
            } else {
                eprintln!("btsnoop: Unrecognized format. Using pcap");
                Format::Pcap
            }
        }
        None => Format::Pcap,
    };

    let out_writer = match args.value_of("output") {
        Some(x) => {
            let path = Path::new(x);
            eprintln!(
                "btsnoop: Outputting to {}",
                path.file_name().unwrap().to_string_lossy()
            );
            Box::new(File::create(&path).unwrap()) as Box<io::Write>
        }
        None => Box::new(io::stdout()) as Box<io::Write>,
    };

    match start(&device_path, out_writer, format, count) {
        Err(err) => eprintln!("btsnoop: failed with error: {}", err),
        _ => {}
    };
}
