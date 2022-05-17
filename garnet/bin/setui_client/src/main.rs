// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_syslog as syslog;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-client"]).expect("Can't init logger");

    println!(
        "setui_client is DEPRECATED, please use `ffx setui` instead. Opt in by running a command: \
        {cmd}. More information can be found in {here}.",
        cmd = "ffx config set setui true",
        here = "https://fuchsia.dev/reference/tools/sdk/ffx#setui"
    );

    Ok(())
}
