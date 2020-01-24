// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::{log_errors, Observer},
    router::Router,
    runtime::{spawn, wait_until},
};
use anyhow::format_err;
use futures::prelude::*;
use std::{
    rc::{Rc, Weak},
    time::{Duration, Instant},
};

pub fn spawn_link_status_updater(router: &Rc<Router>, mut state_change_observer: Observer<()>) {
    let router = Rc::downgrade(router);
    spawn(log_errors(
        async move {
            while state_change_observer.next().await.is_some() {
                Weak::upgrade(&router)
                    .ok_or_else(|| format_err!("Router gone"))?
                    .publish_new_link_status()
                    .await;
                wait_until(Instant::now() + Duration::from_millis(250)).await;
            }
            Ok(())
        },
        "Failed propagating link status",
    ));
}
