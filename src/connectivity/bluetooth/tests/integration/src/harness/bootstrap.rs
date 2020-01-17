// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_sys::{BootstrapMarker, BootstrapProxy},
    fuchsia_bluetooth::expectation::asynchronous::ExpectationHarness,
    futures::future::{self, BoxFuture, FutureExt},
};

use crate::harness::{control::ActivatedFakeHost, TestHarness};

pub type BootstrapHarness = ExpectationHarness<(), BootstrapProxy>;

impl TestHarness for BootstrapHarness {
    type Env = ActivatedFakeHost;
    type Runner = future::Pending<Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let fake_host = ActivatedFakeHost::new("bt-hci-integration-bootstrap-0").await?;
            match fuchsia_component::client::connect_to_service::<BootstrapMarker>() {
                Ok(proxy) => Ok((BootstrapHarness::new(proxy), fake_host, future::pending())),
                Err(e) => Err(format_err!("Failed to connect to Bootstrap service: {:?}", e)),
            }
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}
