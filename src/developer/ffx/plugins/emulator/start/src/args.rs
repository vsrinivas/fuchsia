// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_config::FfxConfigBacked;
use ffx_core::ffx_command;
use ffx_emulator_config::{AccelerationMode, EngineType, GpuType, NetworkingMode};
use std::path::PathBuf;

#[ffx_command()]
#[derive(Clone, FromArgs, FfxConfigBacked, Debug, Default, PartialEq)]
#[argh(
    subcommand,
    name = "start",
    description = "Start the Fuchsia emulator.",
    example = "ffx emu start
ffx emu start workstation.qemu-x64 --name my-emulator --engine femu",
    note = "The `start` subcommand is the starting point for all emulator interactions.
The name provided here will be used for all later interactions to indicate
which emulator to target. Emulator names must be unique.

The start command will compile all of the necessary configuration for an 
emulator, launch the emulator, and then store the configuration on disk for 
future reference. The configuration comes from the Product Bundle, which 
includes a virtual device specification and a start-up flag template. See 
https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0100_product_metadata
for more information."
)]
pub struct StartCommand {
    /// virtualization acceleration. Valid choices are "none" to disable acceleration, "hyper" to
    /// use the host's hypervisor interface (KVM on Linux and HVF on MacOS), or "auto" to use the
    /// hypervisor if detected. The default value is "auto".
    #[argh(option, default = "AccelerationMode::Auto")]
    pub accel: AccelerationMode,

    /// launch the emulator in serial console mode. This redirects the virtual serial port to the
    /// host's input/output streams, then maintains a connection to those streams rather than
    /// returning control to the host terminal. This is especially useful when the guest is running
    /// without networking enabled.
    #[argh(switch)]
    pub console: bool,

    /// pause on launch and wait for a debugger process to attach before resuming. The guest
    /// operating system will not begin execution until a debugger, such as gdb or lldb, attaches
    /// to the emulator process and signals the emulator to continue.
    #[argh(switch)]
    pub debugger: bool,

    /// sets up the emulation, but doesn't start the emulator. The command line arguments that the
    /// current configuration generates will be printed to stdout for review.
    #[argh(switch)]
    pub dry_run: bool,

    /// emulation engine to use for this instance. Allowed values are "femu" which is based on
    /// Android Emulator, and "qemu" which uses the version of Qemu packaged with Fuchsia. Default
    /// is "femu".
    #[argh(option, default = "EngineType::Femu")]
    pub engine: EngineType,

    /// GPU acceleration mode. Allowed values are "host", "guest", "swiftshader_indirect", or
    /// "auto". Default is "auto". Note: this is unused when using the "qemu" engine type. See
    /// https://developer.android.com/studio/run/emulator-acceleration#command-gpu for details on
    /// the available options.
    #[argh(option, default = "GpuType::Auto")]
    pub gpu: GpuType,

    /// run the emulator without a GUI. The guest system may still initialize graphics drivers,
    /// but no graphics interface will be presented to the user.
    #[argh(switch, short = 'H')]
    pub headless: bool,

    /// enable pixel scaling on HiDPI devices. Defaults to true for MacOS, false otherwise.
    #[argh(option, default = "std::env::consts::OS == \"macos\"")]
    pub hidpi_scaling: bool,

    /// store the emulator log at the provided filesystem path. By default, all output goes to
    /// stdout/stderr.
    #[argh(option, short = 'l')]
    pub log: Option<PathBuf>,

    /// launch the emulator in Qemu monitor console mode. See
    /// https://qemu-project.gitlab.io/qemu/system/monitor.html for more information on the Qemu
    /// monitor console.
    #[argh(switch, short = 'm')]
    pub monitor: bool,

    /// name of this emulator instance. This is used to identify the instance in other commands and
    /// tools. Default is "fuchsia-emulator".
    #[argh(option, default = "\"fuchsia-emulator\".to_string()")]
    pub name: String,

    /// specify the networking mode for the emulator. Allowed values are "none" which disables
    /// networking, "tap" which attaches to a Tun/Tap interface, "user" which sets up mapped ports
    /// via SLiRP, and "auto" which will check the host system's capabilities and select "tap" if
    /// it is available and "user" otherwise. Default is "auto".
    #[argh(option, default = "NetworkingMode::Auto")]
    pub net: NetworkingMode,

    /// specify a host port mapping for user-networking mode. Ignored in other networking modes.
    /// Syntax is "--port-map <portname>:<port>". The <portname> must be one of those specified in
    /// the virtual device specification. This flag may be repeated for multiple port mappings.
    #[argh(option)]
    pub port_map: Vec<String>,

    /// use named product information from Product Bundle Metadata (PBM). If no
    /// product bundle is specified and there is an obvious choice, that will be
    /// used (e.g. if there is only one PBM available).
    #[argh(positional)]
    pub product_bundle: Option<String>,

    /// specify a template file to populate the command line flags for the emulator.
    /// Defaults to a Handlebars template specified in the Product Bundle manifest.
    // TODO(fxbug.dev/90948): Make this non-optional once the template file is
    // included in the SDK.
    #[argh(option)]
    pub start_up_args_template: Option<PathBuf>,

    /// the maximum time (in seconds) to wait on an emulator to boot before returning control
    /// to the user. A value of 0 will skip the check entirely. Default is 60 seconds. This
    /// can be overridden with `ffx config set emu.start.timeout <seconds>`.
    #[argh(option, short = 's')]
    #[ffx_config_default(key = "emu.start.timeout", default = "60")]
    pub startup_timeout: Option<u64>,

    /// enables extra logging for debugging.
    #[argh(switch, short = 'V')]
    pub verbose: bool,
}
