// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{format_err, Error, ResultExt},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fidl::endpoints::{ClientEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy
    },
    fuchsia_component::client::connect_to_service,
};

pub async fn list_directory<'a>(root_proxy: &'a DirectoryProxy) -> Vec<String> {
    let entries = await!(files_async::readdir(&root_proxy)).expect("readdir failed");
    let mut items = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
    items.sort();
    items
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let hub_chan = io_util::open_directory_in_namespace("/hub", io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE)
        .unwrap()
        .into_channel()
        .unwrap()
        .into_zx_channel();
    let hub_proxy = ClientEnd::<DirectoryMarker>::new(hub_chan).into_proxy().
        expect("Failed to create directory proxy");
    let directory_listing = await!(list_directory(&hub_proxy)).join(",");

    let echo = connect_to_service::<fecho::EchoMarker>().context("error connecting to echo")?;
    let out = await!(echo.echo_string(Some(&directory_listing))).context("echo_string failed")?;
    println!("{}", out.ok_or(format_err!("empty result"))?);
    Ok(())
}
