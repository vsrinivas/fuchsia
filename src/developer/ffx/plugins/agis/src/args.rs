// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "agis", description = "Agis Service")]
pub struct AgisCommand {
    #[argh(subcommand)]
    pub operation: Operation,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum Operation {
    Register(RegisterOp),
    Vtcs(VtcsOp),
    Listen(ListenOp),
    Shutdown(ShutdownOp),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "register", description = "register a process with AGIS for tracing")]
pub struct RegisterOp {
    #[argh(positional, description = "unique id for this registration")]
    pub id: u64,

    #[argh(positional, description = "process koid")]
    pub process_koid: u64,

    #[argh(positional, description = "process name")]
    pub process_name: String,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "vtcs", description = "list all vulkan traceable components")]
pub struct VtcsOp {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "listen", description = "initiate listening on |global_id|")]
pub struct ListenOp {
    #[argh(positional, description = "global id on which to listen")]
    pub global_id: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "shutdown", description = "shutdown all listeners")]
pub struct ShutdownOp {}
