// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use fidl_fuchsia_validate_logs::EncodingPuppetMarker;
use fuchsia_async::Socket;
use fuchsia_component::client::{launch, launcher};
use futures::channel::mpsc::channel;
use futures::channel::mpsc::Receiver;
use futures::channel::mpsc::Sender;
use futures::prelude::*;

mod encoding;
mod sink;

/// Validate Log VMO formats written by 'puppet' programs controlled by
/// this Validator program.
#[derive(Debug, FromArgs)]
struct Opt {
    /// required arg: The URL of the puppet
    #[argh(option, long = "url")]
    puppet_url: String,
    /// required arg: Whether or not to test the log sink
    #[argh(switch)]
    test_log_sink: bool,
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&[]).unwrap();
    let Opt { puppet_url, test_log_sink } = argh::from_env();
    let (tx, mut rx): (Sender<fidl::Socket>, Receiver<fidl::Socket>) = channel(1);

    let (_env, app) = if test_log_sink {
        sink::launch_puppet(&puppet_url, tx)?
    } else {
        let launcher = launcher().unwrap();
        let app = launch(&launcher, puppet_url.to_string(), None)?;
        (None, app)
    };
    let proxy = app.connect_to_service::<EncodingPuppetMarker>()?;
    encoding::test_encodings(proxy).await?;

    if test_log_sink {
        let socket = Socket::from_socket(rx.next().await.unwrap()).unwrap();
        sink::test_socket(&socket, puppet_url).await;
    }

    Ok(())
}
