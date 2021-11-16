// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Default, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "lock", description = "Fastboot lock a target")]
pub struct FlashLockCommand {}
