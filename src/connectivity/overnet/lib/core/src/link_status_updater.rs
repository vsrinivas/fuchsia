// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::log_errors,
    router::Router,
    runtime::{spawn, wait_until},
};
use anyhow::{bail, format_err};
use futures::{prelude::*, select, stream::SelectAll};
use std::{
    pin::Pin,
    rc::{Rc, Weak},
    time::{Duration, Instant},
};

pub type StatusChangeStream = Pin<Box<dyn Stream<Item = ()>>>;
pub type LinkStatePublisher = futures::channel::mpsc::Sender<StatusChangeStream>;
type LinkStateReceiver = futures::channel::mpsc::Receiver<StatusChangeStream>;

pub fn spawn_link_status_updater(router: &Rc<Router>, mut receiver: LinkStateReceiver) {
    let router = Rc::downgrade(router);
    spawn(log_errors(
        async move {
            let mut select_all = SelectAll::new();
            loop {
                enum Action {
                    Publish(Option<()>),
                    Register(Option<StatusChangeStream>),
                };
                let action = select! {
                    x = select_all.next().fuse() => Action::Publish(x),
                    x = receiver.next().fuse() => Action::Register(x),
                };
                match action {
                    Action::Publish(Some(())) => {
                        Weak::upgrade(&router)
                            .ok_or_else(|| format_err!("Router gone"))?
                            .publish_new_link_status()
                            .await;
                        wait_until(Instant::now() + Duration::from_millis(250)).await;
                    }
                    Action::Publish(None) => {
                        select_all.push(
                            receiver
                                .next()
                                .await
                                .ok_or_else(|| format_err!("Registration channel is gone"))?,
                        );
                    }
                    Action::Register(Some(s)) => {
                        select_all.push(s);
                    }
                    Action::Register(None) => bail!("Registration channel is gone"),
                }
            }
        },
        "Failed propagating link status",
    ));
}
