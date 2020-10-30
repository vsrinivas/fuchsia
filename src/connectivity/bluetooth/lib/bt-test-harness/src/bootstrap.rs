// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_sys::{BootstrapMarker, BootstrapProxy},
    fuchsia_bluetooth::expectation::asynchronous::{expectable, Expectable},
    futures::future::{self, BoxFuture, FutureExt},
    std::ops::{Deref, DerefMut},
    test_harness::TestHarness,
};

use crate::deprecated::control::ActivatedFakeHost;

#[derive(Clone)]
pub struct BootstrapHarness(Expectable<(), BootstrapProxy>);

impl Deref for BootstrapHarness {
    type Target = Expectable<(), BootstrapProxy>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for BootstrapHarness {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl TestHarness for BootstrapHarness {
    type Env = ActivatedFakeHost;
    type Runner = future::Pending<Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let fake_host = ActivatedFakeHost::new("bt-hci-integration-bootstrap-0").await?;
            match fuchsia_component::client::connect_to_service::<BootstrapMarker>() {
                Ok(proxy) => Ok((
                    BootstrapHarness(expectable(Default::default(), proxy)),
                    fake_host,
                    future::pending(),
                )),
                Err(e) => Err(format_err!("Failed to connect to Bootstrap service: {:?}", e)),
            }
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}
