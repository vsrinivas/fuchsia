// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::{FromArgValue, FromArgs},
    std::fmt,
};

#[derive(Copy, Clone, Debug, PartialEq)]
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
    pub fn guest_manager_interface(&self) -> &str {
        match *self {
            GuestType::Zircon => "fuchsia.virtualization.ZirconGuestManager",
            GuestType::Debian => "fuchsia.virtualization.DebianGuestManager",
            GuestType::Termina => "fuchsia.virtualization.TerminaGuestManager",
        }
    }

    pub fn gn_target_label(self) -> &'static str {
        match self {
            GuestType::Zircon => "//src/virtualization/bundles:zircon",
            GuestType::Debian => "//src/virtualization/bundles:debian",
            GuestType::Termina => "//src/virtualization/bundles:termina",
        }
    }

    pub fn gn_core_shard_label(&self) -> &'static str {
        match self {
            GuestType::Zircon => "//src/virtualization/bundles:zircon_core_shards",
            GuestType::Debian => "//src/virtualization/bundles:debian_core_shards",
            GuestType::Termina => "//src/virtualization/bundles:termina_core_shards",
        }
    }

    pub fn package_url(&self) -> &'static str {
        match self {
            GuestType::Zircon => "fuchsia-pkg://fuchsia.com/zircon_guest#meta/zircon_guest.cm",
            GuestType::Debian => "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cm",
            GuestType::Termina => "fuchsia-pkg://fuchsia.com/termina_guest#meta/termina_guest.cm",
        }
    }

    pub fn all_guests() -> Vec<GuestType> {
        vec![GuestType::Debian, GuestType::Termina, GuestType::Zircon]
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
    Stop(StopArgs),
    Balloon(BalloonArgs),
    BalloonStats(BalloonStatsArgs),
    Serial(SerialArgs),
    List(ListArgs),
    Socat(SocatArgs),
    SocatListen(SocatListenArgs),
    Vsh(VshArgs),
    VsockPerf(VsockPerfArgs),
    Wipe(WipeArgs),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Modify the size of a memory balloon. Usage: guest balloon guest-type num-pages
#[argh(subcommand, name = "balloon")]
pub struct BalloonArgs {
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
    #[argh(positional)]
    /// number of pages guest balloon will have after use.
    pub num_pages: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// See the stats of a guest's memory balloon. Usage: guest balloon-stats guest-type
#[argh(subcommand, name = "balloon-stats")]
pub struct BalloonStatsArgs {
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Access the serial output for a guest. Usage: guest serial guest-type
#[argh(subcommand, name = "serial")]
pub struct SerialArgs {
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
}

#[derive(FromArgs, PartialEq, Debug)]
/// List available guest environments. Usage: guest list [guest-type]
#[argh(subcommand, name = "list")]
pub struct ListArgs {
    #[argh(positional)]
    /// optional guest type to get detailed information about
    pub guest_type: Option<GuestType>,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Create a socat connection on the specified port. Usage: guest socat guest-type port
#[argh(subcommand, name = "socat")]
pub struct SocatArgs {
    #[argh(option)]
    /// type of the guest
    pub guest_type: GuestType,
    #[argh(option)]
    /// port for listeners to connect on.
    pub port: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Listen through socat on the specified port. Usage: guest socat-listen guest-type host-port
#[argh(subcommand, name = "socat-listen")]
pub struct SocatListenArgs {
    #[argh(option)]
    /// type of the guest
    pub guest_type: GuestType,
    #[argh(option)]
    /// port number of host (see `guest socat`)
    pub host_port: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Create virtual shell for a guest or connect via virtual shell. Usage: guest vsh [guest-type[port]] [--args <arg>]
#[argh(subcommand, name = "vsh")]
pub struct VshArgs {
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
/// Perform a vsock micro benchmark on the target guest. Only Debian is supported.
#[argh(subcommand, name = "vsock-perf")]
pub struct VsockPerfArgs {
    #[argh(positional)]
    /// type of the guest
    pub guest_type: GuestType,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Launch a guest image. Usage: guest launch guest_type [--cmdline-add <arg>...] [--default-net <bool>] [--memory <memory-size>] [--cpus <num-cpus>] [--virtio-* <bool>]
#[argh(subcommand, name = "launch")]
pub struct LaunchArgs {
    #[argh(positional)]
    /// guest type to launch e.g. 'zircon'.
    pub guest_type: GuestType,
    /// adds provided strings to the existing kernel command line
    #[argh(option)]
    pub cmdline_add: Vec<String>,
    /// enable a default net device
    #[argh(option)]
    pub default_net: Option<bool>,
    /// allocate 'bytes' of memory for the guest
    #[argh(option)]
    pub memory: Option<u64>,
    /// number of virtual cpus available for the guest
    #[argh(option)]
    pub cpus: Option<u8>,
    /// enable virtio-balloon
    #[argh(option)]
    pub virtio_balloon: Option<bool>,
    /// enable virtio-console
    #[argh(option)]
    pub virtio_console: Option<bool>,
    /// enable virtio-gpu and virtio-input
    #[argh(option)]
    pub virtio_gpu: Option<bool>,
    /// enable virtio-rng
    #[argh(option)]
    pub virtio_rng: Option<bool>,
    /// enable virtio-sound
    #[argh(option)]
    pub virtio_sound: Option<bool>,
    /// enable virtio-sound-input
    #[argh(option)]
    pub virtio_sound_input: Option<bool>,
    /// enable virtio-vsock
    #[argh(option)]
    pub virtio_vsock: Option<bool>,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Stop a running guest. Usage: guest stop guest_type [-f]
#[argh(subcommand, name = "stop")]
pub struct StopArgs {
    /// guest type to launch e.g. 'zircon'
    #[argh(positional)]
    pub guest_type: GuestType,
    /// force stop the guest
    #[argh(switch, short = 'f')]
    pub force: bool,
}
