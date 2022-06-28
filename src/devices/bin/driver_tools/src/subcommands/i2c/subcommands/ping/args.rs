// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "ping",
    description = "Pings all I2C devices found in /dev/class/i2c/",
    example = "Ping all I2C devices found in /dev/class/i2c/:

    $ driver i2cutil ping"
)]
pub struct PingCommand {}
