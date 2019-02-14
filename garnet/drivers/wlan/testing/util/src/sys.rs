// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use failure::{Error, err_msg};
use fdio;
use std::fs::File;
use fidl_fuchsia_device::ControllerSynchronousProxy;
use fidl_fuchsia_device_test::{DeviceSynchronousProxy, RootDeviceSynchronousProxy};

use super::open_rdwr;

pub fn create_test_device(test_path: &str, dev_name: &str) -> Result<String, Error> {
    let test_dev = open_rdwr(test_path)?;
    let channel = fdio::clone_channel(&test_dev)?;
    let mut interface = RootDeviceSynchronousProxy::new(channel);

    let (status, devpath) = interface.create_device(dev_name, fuchsia_zircon::Time::INFINITE)?;
    fuchsia_zircon::Status::ok(status)?;
    devpath.ok_or(err_msg("RootDevice.CreateDevice received no devpath?"))
}

pub fn bind_test_device(device: &File, driver_name: &str) -> Result<(), Error> {
    let channel = fdio::clone_channel(device)?;
    let mut interface = ControllerSynchronousProxy::new(channel);
    let status = interface.bind(driver_name, fuchsia_zircon::Time::INFINITE)?;
    fuchsia_zircon::Status::ok(status)?;
    Ok(())
}

pub fn destroy_test_device(device: &File) -> Result<(), Error> {
    let channel = fdio::clone_channel(device)?;
    let mut interface = DeviceSynchronousProxy::new(channel);
    Ok(interface.destroy()?)
}
