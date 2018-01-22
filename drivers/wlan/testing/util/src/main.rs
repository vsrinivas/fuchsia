// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[macro_use]
extern crate failure;
#[macro_use]
extern crate fdio;
extern crate fuchsia_wlan_dev as wlan_dev;
extern crate fuchsia_zircon as zircon;
extern crate garnet_lib_wlan_fidl as wlan;

use failure::Error;
use std::fs::{File, OpenOptions};
use std::path::Path;

mod sys;

const DEV_TEST: &str = "/dev/misc/test";
const DEV_WLANPHY: &str = "wlanphy-test";
const WLAN: &str = "wlan";
const WLAN_DRIVER_NAME: &str = "/system/driver/wlanphy-testdev.so";

fn usage(appname: &str) {
    eprintln!("usage: {} [add|rm|query|create|destroy <n>]", appname);
}

fn open_rdwr<P: AsRef<Path>>(path: P) -> Result<File, Error> {
    OpenOptions::new().read(true).write(true).open(path).map_err(|e| e.into())
}

fn open_wlanphy_device() -> Result<File, Error> {
    let path = Path::new(DEV_TEST).join(WLAN).join(DEV_WLANPHY);
    open_rdwr(path)
}

fn add_wlanphy() -> Result<(), Error> {
    let devpath = sys::create_test_device(DEV_TEST, WLAN)?;
    eprintln!("created test device at {:?}", devpath.to_string_lossy());

    // The device created above might not show up in the /dev filesystem right away. Loop until we
    // have the device opened (or we give up).
    // Note: a directory watcher could work too, but may be a bit heavy for this use-case.
    let mut retry = 0;
    let mut dev = None;
    {
        while retry < 100 {
            retry += 1;
            if let Ok(d) = open_rdwr(&devpath) {
                dev = Some(d);
                break;
            }
        }
    }
    let dev = dev.ok_or(format_err!("could not open {:?}", devpath.to_string_lossy()))?;

    sys::bind_test_device(&dev, WLAN_DRIVER_NAME)
}

fn rm_wlanphy() -> Result<(), Error> {
    let path = Path::new(DEV_TEST).join(WLAN);
    let dev = open_rdwr(path.clone())?;

    sys::destroy_test_device(&dev).map(|_| eprintln!("{:?} destroyed", path))
}

fn query_wlanphy() -> Result<(), Error> {
    let dev = open_wlanphy_device()?;
    wlan_dev::sys::query_wlanphy_device(&dev).map(|info| eprintln!("query results: {:?}", info))
}

fn create_wlanintf() -> Result<(), Error> {
    let dev = open_wlanphy_device()?;
    wlan_dev::sys::create_wlaniface(&dev, wlan::MacRole::Client).map(|i| eprintln!("create results: {:?}", i))
}

fn destroy_wlanintf(id: u16) -> Result<(), Error> {
    let dev = open_wlanphy_device()?;
    wlan_dev::sys::destroy_wlaniface(&dev, id)
        .map(|_| eprintln!("destroyed intf {}", id))
        .map_err(|e| e.into())
}

fn main() {
    if let Err(e) = main_res() {
        eprintln!("Error: {}", e);
    }
}

fn main_res() -> Result<(), Error> {
    let args: Vec<_> = std::env::args().collect();
    let appname = &args[0];
    if args.len() < 2 {
        usage(appname);
        return Ok(());
    }
    let command = &args[1];

    match command.as_ref() {
        "add" => add_wlanphy(),
        "rm" => rm_wlanphy(),
        "query" => query_wlanphy(),
        "create" => create_wlanintf(),
        "destroy" => {
            if args.len() < 3 {
                usage(appname);
                Ok(())
            } else {
                let id = u16::from_str_radix(&args[2], 10)?;
                destroy_wlanintf(id)
            }
        },
        _ => {
            usage(appname);
            Ok(())
        }
    }
}


