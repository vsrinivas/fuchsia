// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(target_os = "fuchsia")]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_overnet::OvernetMarker,
    fuchsia_component,
    futures::prelude::*,
};

pub use fidl_fuchsia_overnet::OvernetProxyInterface;
pub use fuchsia_async::spawn_local as spawn;

pub fn connect() -> Result<impl OvernetProxyInterface, Error> {
    Ok(fuchsia_component::client::connect_to_service::<OvernetMarker>()
        .context("Failed to connect to overnet service")?)
}

pub fn run(f: impl Future<Output = ()> + 'static) {
    fuchsia_async::Executor::new().unwrap().run_singlethreaded(f)
}
