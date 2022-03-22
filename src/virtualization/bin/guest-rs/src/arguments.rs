// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, PartialEq, Debug)]
/// Launch a guest image. Usage: guest-rs launch --package <package> [--vmm_args <args>]
#[argh(subcommand, name = "launch")]
pub struct LaunchArgs {
    #[argh(positional)]
    /// package name to launch e.g. 'zircon_guest'.
    pub package: String,
    /// adds provided strings to the existing kernel command line
    #[argh(option)]
    pub cmdline_add: Vec<String>,
    /// enable a default net device (defaults to true)
    #[argh(option, default = "true")]
    pub default_net: bool,
    /// allocate 'bytes' of memory for the guest
    #[argh(option)]
    pub memory: Option<u64>,
    /// number of virtual cpus available for the guest
    #[argh(option)]
    pub cpus: Option<u8>,
    /// add hardware interrupt mapping for the guest
    #[argh(option)]
    pub interrupt: Vec<u32>,
    /// enable virtio-balloon (defaults to true)
    #[argh(option, default = "true")]
    pub virtio_balloon: bool,
    /// enable virtio-console (defaults to true)
    #[argh(option, default = "true")]
    pub virtio_console: bool,
    /// enable virtio-gpu and virtio-input (defaults to true)
    #[argh(option, default = "true")]
    pub virtio_gpu: bool,
    /// enable virtio-rng (defaults to true)
    #[argh(option, default = "true")]
    pub virtio_rng: bool,
    /// enable virtio-sound (defaults to true)
    #[argh(option, default = "true")]
    pub virtio_sound: bool,
    /// enable virtio-sound-input (defaults to false)
    #[argh(option, default = "false")]
    pub virtio_sound_input: bool,
    /// enable virtio-vsock (defaults to true)
    #[argh(option, default = "true")]
    pub virtio_vsock: bool,
}
