// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_syslog as syslog;
use setui_client_lib::*;
use structopt::StructOpt;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-client"]).expect("Can't init logger");

    let command = SettingClient::from_args();

    run_command(command).await?;

    Ok(())
}
