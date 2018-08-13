// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[deny(warnings)]
use failure::Error;
use fuchsia_bluetooth::hci;
use rand::Rng;
use std::fs::{File, OpenOptions};
use std::path::Path;

fn usage(appname: &str) -> () {
    eprintln!("usage: {} [add|rm]", appname);
}

fn open_rdwr<P: AsRef<Path>>(path: P) -> Result<File, Error> {
    OpenOptions::new()
        .read(true)
        .write(true)
        .open(path)
        .map_err(|e| e.into())
}

fn rm_device(dev_name: &str) -> Result<(), Error> {
    let path = Path::new(hci::DEV_TEST).join(dev_name);
    let dev = open_rdwr(path.clone())?;

    hci::destroy_device(&dev).map(|_| println!("{:?} destroyed", path))
}

fn main() {
    let args: Vec<_> = std::env::args().collect();
    let appname = &args[0];
    if args.len() < 2 {
        usage(appname);
        return;
    }
    // TODO(bwb) better arg parsing
    let command = &args[1];

    match command.as_str() {
        "add" => {
            if let Ok((_, id)) = hci::create_and_bind_device() {
                println!("fake device added: {}", id);
            }
        }
        "rm" => {
            if args.len() < 3 {
                usage(appname);
                return;
            }
            rm_device(args[2].as_str());
        }
        _ => {
            usage(appname);
        }
    };
}
