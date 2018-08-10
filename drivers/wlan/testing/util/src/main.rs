// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[macro_use]
extern crate failure;
#[macro_use]
extern crate fdio;
extern crate fidl;
extern crate fidl_fuchsia_wlan_device as wlan;
extern crate fuchsia_async as async;
extern crate fuchsia_wlan_dev as wlan_dev;
extern crate futures;

use failure::{Error, ResultExt};
use futures::prelude::*;
use std::convert::Into;
use std::fs::{File, OpenOptions};
use std::path::Path;

mod sys;

const DEV_TEST: &str = "/dev/test/test";
const DEV_WLANPHY: &str = "/dev/test/test/wlan/wlanphy-test";
const WLAN: &str = "wlan";
const WLAN_DRIVER_NAME: &str = "/system/driver/wlanphy-testdev.so";

fn usage(appname: &str) {
    eprintln!("usage: {} [add|rm|query|create|destroy <n>]", appname);
}

fn open_rdwr<P: AsRef<Path>>(path: P) -> Result<File, Error> {
    OpenOptions::new().read(true).write(true).open(path).map_err(Into::into)
}

fn get_proxy() -> Result<(async::Executor, wlan::PhyProxy), Error> {
    let executor = async::Executor::new().context("error creating event loop")?;

    let phy = wlan_dev::Device::new(DEV_WLANPHY)?;
    let proxy = wlan_dev::connect_wlan_phy(&phy)?;
    Ok((executor, proxy))
}

fn add_wlanphy() -> Result<(), Error> {
    let devpath = sys::create_test_device(DEV_TEST, WLAN)?;
    println!("created test device at {:?}", devpath);

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
    let dev = dev.ok_or(format_err!("could not open {:?}", devpath))?;

    sys::bind_test_device(&dev, WLAN_DRIVER_NAME)
}

fn rm_wlanphy() -> Result<(), Error> {
    let path = Path::new(DEV_TEST).join(WLAN);
    let dev = open_rdwr(path.clone())?;

    sys::destroy_test_device(&dev)
        .map(|_| println!("{:?} destroyed", path))
}

fn query_wlanphy() -> Result<(), Error> {
    let (mut executor, proxy) = get_proxy()?;
    let fut = proxy.query().map_ok(|resp| {
       println!("query results: {:?}", resp.info);
    });
    executor.run_singlethreaded(fut).map_err(Into::into)
}

fn create_wlanintf() -> Result<(), Error> {
    let (mut executor, proxy) = get_proxy()?;
    let mut req = wlan::CreateIfaceRequest { role: wlan::MacRole::Client };
    let fut = proxy.create_iface(&mut req).map_ok(|resp| {
       println!("create results: {:?}", resp);
    });
    executor.run_singlethreaded(fut).map_err(Into::into)
}

fn destroy_wlanintf(id: u16) -> Result<(), Error> {
    let (mut executor, proxy) = get_proxy()?;
    let mut req = wlan::DestroyIfaceRequest { id: id };
    let fut = proxy.destroy_iface(&mut req).map_ok(|resp| {
       println!("destroyed intf {} resp: {:?}", id, resp);
    });
    executor.run_singlethreaded(fut).map_err(Into::into)
}

fn main() -> Result<(), Error> {
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


