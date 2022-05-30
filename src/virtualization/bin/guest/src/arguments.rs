// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::{FromArgValue, FromArgs},
    std::fmt,
};

#[derive(Debug, PartialEq)]
pub enum GuestType {
    Debian,
    Termina,
    Zircon,
}

impl FromArgValue for GuestType {
    fn from_arg_value(value: &str) -> Result<Self, String> {
        match value {
            "debian" => Ok(Self::Debian),
            "termina" => Ok(Self::Termina),
            "zircon" => Ok(Self::Zircon),
            _ => Err(format!(
                "Unrecognized guest type \"{}\". Supported guest types are: \
                \"debian\", \"termina\", \"zircon\".",
                value
            )),
        }
    }
}

impl fmt::Display for GuestType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            GuestType::Debian => write!(f, "debian"),
            GuestType::Termina => write!(f, "termina"),
            GuestType::Zircon => write!(f, "zircon"),
        }
    }
}

impl GuestType {
    pub fn package_url(&self) -> &str {
        match *self {
            GuestType::Debian => "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx",
            GuestType::Termina => "fuchsia-pkg://fuchsia.com/termina_guest#meta/termina_guest.cmx",
            GuestType::Zircon => "fuchsia-pkg://fuchsia.com/zircon_guest#meta/zircon_guest.cmx",
        }
    }
    pub fn guest_manager_interface(&self) -> &str {
        match *self {
            GuestType::Zircon => "fuchsia.virtualization.ZirconGuestManager",
            GuestType::Debian => "fuchsia.virtualization.DebianGuestManager",
            GuestType::Termina => "fuchsia.virtualization.TerminaGuestManager",
        }
    }
}

#[derive(FromArgs, PartialEq, Debug)]
/// Launch a guest image. Usage: guest launch guest_type [--cmdline-add <arg>...] [--interrupt <interrupt>...] [--default-net <bool>] [--memory <memory-size>] [--cpus <num-cpus>] [--virtio-* <bool>]
#[argh(subcommand, name = "launch")]
pub struct LaunchArgs {
    #[argh(positional)]
    /// guest type to launch e.g. 'zircon'.
    pub guest_type: GuestType,
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
