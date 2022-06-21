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
/// Top-level command.
pub struct GuestOptions {
    #[argh(subcommand)]
    pub nested: SubCommands,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommands {
    Launch(LaunchArgs),
    Balloon(BalloonArgs),
    BalloonStats(BalloonStatsArgs),
    Serial(SerialArgs),
    List(ListArgs),
    Socat(SocatArgs),
    SocatListen(SocatListenArgs),
    Vsh(VshArgs),
    Wipe(WipeArgs),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Modify the size of a memory balloon. Usage: guest balloon env-id cid num-pages
#[argh(subcommand, name = "balloon")]
pub struct BalloonArgs {
    #[argh(option)]
    /// environment id where guest lives.
    pub env_id: Option<u32>,
    #[argh(option)]
    /// context id of guest.
    pub cid: Option<u32>,
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
    #[argh(positional)]
    /// number of pages guest balloon will have after use.
    pub num_pages: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// See the stats of a guest's memory balloon. Usage: guest balloon-stats env-id cid
#[argh(subcommand, name = "balloon-stats")]
pub struct BalloonStatsArgs {
    #[argh(option)]
    /// environment id where guest lives.
    pub env_id: Option<u32>,
    #[argh(option)]
    /// context id of guest.
    pub cid: Option<u32>,
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Access the serial output for a guest. Usage: guest serial env-id cid
#[argh(subcommand, name = "serial")]
pub struct SerialArgs {
    #[argh(option)]
    /// environment id where guest lives.
    pub env_id: Option<u32>,
    #[argh(option)]
    /// context id of guest.
    pub cid: Option<u32>,
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
}

#[derive(FromArgs, PartialEq, Debug)]
/// List existing guest environments. Usage: guest list
#[argh(subcommand, name = "list")]
pub struct ListArgs {}

#[derive(FromArgs, PartialEq, Debug)]
/// Create a socat connection on the specified port. Usage: guest socat env-id port
#[argh(subcommand, name = "socat")]
pub struct SocatArgs {
    #[argh(option)]
    /// environment id where guest lives.
    pub env_id: Option<u32>,
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
    #[argh(option)]
    /// port for listeners to connect on.
    pub port: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Listen through socat on the specified port. Usage: guest socat-listen env-id host-port
#[argh(subcommand, name = "socat-listen")]
pub struct SocatListenArgs {
    #[argh(option)]
    /// environment id of host.
    pub env_id: Option<u32>,
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
    #[argh(option)]
    /// port number of host (see `guest socat`)
    pub host_port: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Create virtual shell for a guest or connect via virtual shell. Usage: guest vsh [env_id [cid [port]]] [--args <arg>]
#[argh(subcommand, name = "vsh")]
pub struct VshArgs {
    #[argh(option)]
    /// optional environment id of host.
    pub env_id: Option<u32>,
    #[argh(option)]
    /// optional context id of vsh to connect to.
    pub cid: Option<u32>,
    #[argh(option)]
    /// positional port of a vsh socket to connect to.
    pub port: Option<u32>,
    #[argh(option)]
    /// list of arguments to run non-interactively on launch.
    pub args: Vec<String>,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Clears the stateful data for the target guest. Currently only termina is supported.
#[argh(subcommand, name = "wipe")]
pub struct WipeArgs {
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
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
